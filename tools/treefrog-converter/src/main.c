/* SPDX-License-Identifier: MPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int tf_open(const void*,size_t);int tf_runtime(const void*,size_t);const char*tf_error(void);const char*tf_report(void);void tf_close(void);int tf_convert(unsigned,const char*,const char*);const void*tf_output_data(void);size_t tf_output_size(void);
static void*read_file(const char*path,size_t*size){FILE*f=fopen(path,"rb");if(!f)return NULL;if(fseek(f,0,SEEK_END)||ftell(f)<0){fclose(f);return NULL;}*size=(size_t)ftell(f);rewind(f);void*b=malloc(*size);if(!b||fread(b,1,*size,f)!=*size){free(b);b=NULL;}fclose(f);return b;}
int main(int argc,char**argv){
    if(argc!=2&&argc!=6&&argc!=7){fprintf(stderr,"usage: tfconvert INPUT.zip [INDEX NAME VERSION OUTPUT.lilpck [RUNTIME.zip]]\n");return 2;}
    size_t n=0,rn=0;void*b=read_file(argv[1],&n),*runtime=NULL;if(!b||!tf_open(b,n)){fprintf(stderr,"%s\n",tf_error());free(b);tf_close();return 1;}
    if(argc==7){runtime=read_file(argv[6],&rn);if(!runtime||!tf_runtime(runtime,rn)){fprintf(stderr,"%s\n",tf_error());free(b);free(runtime);tf_close();return 1;}}
    puts(tf_report());int status=0;
    if(argc>=6){if(!tf_convert((unsigned)strtoul(argv[2],NULL,10),argv[3],argv[4])){fprintf(stderr,"%s\n",tf_error());status=1;}else{FILE*f=fopen(argv[5],"wbx");if(!f){perror("output");status=1;}else{size_t bytes=tf_output_size();if(fwrite(tf_output_data(),1,bytes,f)!=bytes)status=1;if(fclose(f))status=1;}}}
    tf_close();free(b);free(runtime);return status;
}
