// SPDX-License-Identifier: MPL-2.0
// Isolated Playwright browser: no desktop keyboard or user-browser automation.
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
const require=createRequire(import.meta.url);
const {chromium}=require(process.env.TF_PLAYWRIGHT_MODULE || 'playwright');
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
function argument(name,fallback){const i=process.argv.indexOf(name);return i<0?fallback:process.argv[i+1];}
const input=argument('--input',process.env.TF_TEST_ZIP);
if(!input || !fs.statSync(input).isFile())throw Error('Supply --input ZIP or TF_TEST_ZIP (private, outside the served tree)');
const baseURL=new URL(argument('--base-url',process.env.TF_BASE_URL || 'http://127.0.0.1:8777/'));
if(!['http:','https:'].includes(baseURL.protocol))throw Error('Expected HTTP(S) base URL');
if(!baseURL.pathname.endsWith('/'))baseURL.pathname+='/';
const expectedDownload=argument('--expect-download',process.env.TF_EXPECT_DOWNLOAD);
const out=path.resolve(argument('--out',path.join(root,'_build/browser')));
if(!out.startsWith(path.join(root,'_build')+path.sep))throw Error('Test output must stay in converter _build');
fs.mkdirSync(path.join(out,'downloads'),{recursive:true});
const executable=argument('--executable',process.env.TF_BROWSER_EXECUTABLE) || chromium.executablePath();
if(!executable)throw Error('No existing isolated Chromium-compatible browser');
const browser=await chromium.launch({headless:true,executablePath:executable,args:['--disable-gpu']});
const report={passed:false,network:[],errors:[],uploads:false,physical_device:false,base_url:baseURL.href};
try{
 const context=await browser.newContext({viewport:{width:1440,height:1000},acceptDownloads:true,reducedMotion:'reduce'});
 await context.addCookies([{name:'lilsmoke-local-qa',value:'1',url:baseURL.origin}]);
 const page=await context.newPage();
 page.on('pageerror',e=>report.errors.push(e.message));
 page.on('request',async r=>{report.network.push({method:r.method(),url:r.url()});if(!['GET','HEAD'].includes(r.method())||r.postDataBuffer())report.uploads=true;
  if(r.url()===new URL('runtime/runtime.zip',baseURL).href) report.runtime_same_origin_auth=(await r.allHeaders()).cookie?.includes('lilsmoke-local-qa=1')===true;
 });
 page.on('response',r=>{if(r.status()>=400)report.errors.push(`HTTP ${r.status()}: ${r.url()}`);});
 await page.goto(baseURL.href);
 for(const href of ['./source/LilSmoke-Port-Converter-0.2.0-source.zip','./licenses/MPL-2.0.txt']){
  const response=await context.request.get(new URL(href,baseURL).href);if(!response.ok())throw Error(`Missing public asset: ${href}`);
 }
 await page.getByTestId('archive-input').setInputFiles(input);
 await page.getByTestId('analyze').click();
 await page.waitForFunction(()=>['analyzed','error','blocked'].includes(document.querySelector('#app').dataset.state),{},{timeout:180000});
 report.state=await page.locator('#app').getAttribute('data-state');
 report.text=(await page.locator('main').innerText()).slice(0,14000);
 if(report.state!=='analyzed')throw Error(report.text);
 report.auto_selected=await page.locator('#port-select').inputValue()==='0';
 if(!report.auto_selected)throw Error('Single detected port was not selected automatically');
 await page.locator('#port-select').selectOption('0');
 report.selected_text=(await page.locator('main').innerText()).slice(0,14000);
 await page.screenshot({path:path.join(out,'desktop.png'),fullPage:true,animations:'disabled',timeout:120000});
 report.convert_enabled=await page.getByTestId('convert').isEnabled();
 if(report.convert_enabled){
  await page.getByTestId('convert').click();
  await page.waitForFunction(()=>['complete','error'].includes(document.querySelector('#app').dataset.state),{},{timeout:240000});
  if(await page.locator('#app').getAttribute('data-state')!=='complete')throw Error(await page.locator('main').innerText());
  const waiting=page.waitForEvent('download');await page.getByTestId('download').click();const d=await waiting;
  report.download_name=d.suggestedFilename();
  if(expectedDownload && report.download_name!==expectedDownload)throw Error(`Expected ${expectedDownload}, received ${report.download_name}`);
  const output=path.join(out,'downloads',path.basename(d.suggestedFilename()));await d.saveAs(output);report.output=output;report.output_bytes=fs.statSync(output).size;
  await page.screenshot({path:path.join(out,'converted.png'),fullPage:true,animations:'disabled',timeout:120000});
 }
 if(expectedDownload && !report.output)throw Error('Expected a downloadable converted package');
 await page.setViewportSize({width:390,height:844});
 await page.screenshot({path:path.join(out,'mobile.png'),fullPage:true,animations:'disabled',timeout:120000});
 report.overflow=await page.evaluate(()=>({document:document.documentElement.scrollWidth,viewport:innerWidth}));
 if(report.overflow.document>report.overflow.viewport)throw Error('Mobile horizontal overflow');
 await page.getByTestId('reset').click();
 if(await page.locator('#app').getAttribute('data-state')!=='idle')throw Error('Reset failed');
 report.off_prefix=report.network.filter(r=>!r.url.startsWith(baseURL.href)&&!r.url.startsWith('blob:'+baseURL.origin+'/'));
 report.passed=!report.errors.length&&!report.uploads&&!report.off_prefix.length&&report.runtime_same_origin_auth===true;
 if(!report.passed)throw Error(JSON.stringify(report.errors));
}catch(e){report.error=e.stack;throw e;}
finally{fs.writeFileSync(path.join(out,'report.json'),JSON.stringify(report,null,2));await browser.close();}
console.log(JSON.stringify(report,null,2));
