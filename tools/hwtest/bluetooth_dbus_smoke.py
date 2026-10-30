#!/usr/bin/env python3
"""Exercise the real helper against an isolated BlueZ D-Bus contract fixture.

This checks software/protocol behavior, not radios, codecs or physical devices.
Run under dbus-run-session. May use a real MIPS helper through qemu-user.
"""
import argparse
import os
import subprocess
import threading
import time
import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

DBusGMainLoop(set_as_default=True)
os.environ['DBUS_SYSTEM_BUS_ADDRESS']=os.environ['DBUS_SESSION_BUS_ADDRESS']
bus=dbus.SessionBus()
name=dbus.service.BusName('org.bluez',bus)
adapter='/org/bluez/hci0'
device=adapter+'/dev_12_34_56_78_9A_BC'
address='12:34:56:78:9A:BC'
state=dict(power=True,paired=False,connected=False,exists=True,mode='confirm',present=True)
agent={}

class Objects(dbus.service.Object):
    @dbus.service.method('org.freedesktop.DBus.ObjectManager',out_signature='a{oa{sa{sv}}}')
    def GetManagedObjects(self):
        if not state['present']:
            return {}
        result={adapter:{'org.bluez.Adapter1':{
            'Powered':dbus.Boolean(state['power'])}}}
        if state['exists']:
            result[device]={'org.bluez.Device1':{
                'Address':address,'Alias':'QA headset\tName\nInjection',
                'Paired':dbus.Boolean(state['paired']),
                'Connected':dbus.Boolean(state['connected']),
                'UUIDs':dbus.Array(['0000110b-0000-1000-8000-00805f9b34fb'],signature='s')}}
        return result

class Manager(dbus.service.Object):
    @dbus.service.method('org.bluez.AgentManager1',in_signature='os',sender_keyword='sender')
    def RegisterAgent(self,path,capability,sender=None):
        agent.update(path=path,capability=capability,sender=sender)
    @dbus.service.method('org.bluez.AgentManager1',in_signature='o')
    def UnregisterAgent(self,path):
        agent.clear()

class Adapter(dbus.service.Object):
    @dbus.service.method('org.freedesktop.DBus.Properties',in_signature='ssv')
    def Set(self,interface,key,value):
        assert interface=='org.bluez.Adapter1' and key=='Powered'
        state['power']=bool(value)
    @dbus.service.method('org.bluez.Adapter1')
    def StartDiscovery(self): state['scanning']=True
    @dbus.service.method('org.bluez.Adapter1')
    def StopDiscovery(self): state['scanning']=False
    @dbus.service.method('org.bluez.Adapter1',in_signature='o')
    def RemoveDevice(self,path):
        assert path==device
        state['exists']=False

class Device(dbus.service.Object):
    @dbus.service.method('org.freedesktop.DBus.Properties',in_signature='ssv')
    def Set(self,interface,key,value):
        assert interface=='org.bluez.Device1' and key=='Trusted'
        state['trusted']=bool(value)
    @dbus.service.method('org.bluez.Device1')
    def Connect(self): state['connected']=True
    @dbus.service.method('org.bluez.Device1')
    def Disconnect(self): state['connected']=False
    @dbus.service.method('org.bluez.Device1')
    def CancelPairing(self): state['cancelled']=True
    @dbus.service.method('org.bluez.Device1',async_callbacks=('reply','error'))
    def Pair(self,reply,error):
        proxy=dbus.Interface(bus.get_object(agent['sender'],agent['path']),
                             'org.bluez.Agent1')
        def done(*args):
            state['paired']=True
            state['answer']=args
            reply()
        def failed(err):
            state['paired']=False
            error(err)
        mode=state['mode']
        if mode=='confirm':
            proxy.RequestConfirmation(device,dbus.UInt32(123456),reply_handler=done,error_handler=failed)
        elif mode=='pin':
            proxy.RequestPinCode(device,reply_handler=done,error_handler=failed)
        elif mode=='passkey':
            proxy.RequestPasskey(device,reply_handler=done,error_handler=failed)
        elif mode=='display':
            proxy.DisplayPasskey(device,dbus.UInt32(987654),dbus.UInt16(0),reply_handler=done,error_handler=failed)
        elif mode=='unknown-device':
            proxy.RequestConfirmation(adapter+'/dev_00_00_00_00_00_00',dbus.UInt32(123456),reply_handler=done,error_handler=failed)
        elif mode=='authorize':
            proxy.RequestAuthorization(device,reply_handler=done,error_handler=failed)

objects=Objects(bus,'/')
manager=Manager(bus,'/org/bluez')
radio=Adapter(bus,adapter)
pad=Device(bus,device)
loop=GLib.MainLoop()
thread=threading.Thread(target=loop.run,daemon=True)
thread.start()
p=argparse.ArgumentParser()
p.add_argument('--helper',required=True)
p.add_argument('--qemu')
p.add_argument('--sysroot')
a=p.parse_args()
base=([a.qemu,'-cpu','74Kf','-L',a.sysroot] if a.qemu else [])+[a.helper]
checks=0
def check(ok,label):
    global checks
    assert ok,label
    checks+=1
    print('PASS',label,flush=True)
def run(*args):
    cp=subprocess.run(base+list(args),capture_output=True,text=True,timeout=15)
    return cp.returncode,cp.stdout+cp.stderr
def pair(mode,answer=None):
    state.update(mode=mode,paired=False,cancelled=False)
    proc=subprocess.Popen(base+['pair',address],stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT,text=True,bufsize=1)
    output=[]
    # A timer guarantees a failed agent does not stall the test process.
    timer=threading.Timer(10,proc.kill); timer.start()
    try:
        while True:
            line=proc.stdout.readline()
            if not line: break
            output.append(line)
            if line.startswith(('CONFIRM\t','PIN\t','PASSKEY\t')):
                proc.stdin.write((answer or 'no')+'\n'); proc.stdin.flush()
        proc.wait(timeout=5)
    finally:
        timer.cancel()
        if proc.poll() is None: proc.kill(); proc.wait()
    return proc.returncode,''.join(output)
try:
    code,text=run('list')
    check(code==0 and 'ADAPTER\t'+adapter+'\t1' in text,'adapter discovered')
    check('QA headset Name Injection' in text,'device labels cannot inject protocol lines')
    check(code==0 and '\t0\t0\t1' in text,'paired/connected/audio capability parsed')
    check(run('connect',address)[0]==0 and state['connected'],'connect selected device')
    code,text=run('audio',address)
    check(code==0 and text.strip()=='bluealsa:DEV='+address+',PROFILE=a2dp','validated ALSA output')
    check(run('disconnect',address)[0]==0 and not state['connected'],'disconnect selected device')
    check(run('audio',address)[0]!=0,'disconnected audio refused')
    check(run('connect','../../etc/passwd')[0]==2,'invalid MAC rejected')
    check(run('connect','00:00:00:00:00:00')[0]!=0,'unknown device rejected')
    check(run('power','off')[0]==0 and not state['power'],'radio power off')
    check(run('power','on')[0]==0 and state['power'],'radio power on')
    check(run('trust',address)[0]!=0,'unpaired device cannot be trusted')
    code,text=pair('confirm','yes')
    check(code==0 and 'CONFIRM\t123456' in text and state['paired'],'explicit numeric comparison accepted')
    check(run('trust',address)[0]==0 and state['trusted'],'trust only explicitly selected paired device')
    check(run('untrust',address)[0]==0 and not state['trusted'],'explicit untrust')
    code,text=pair('confirm','no')
    check(code!=0 and not state['paired'],'numeric comparison rejected')
    code,text=pair('pin','0000')
    check(code==0 and str(state['answer'][0])=='0000','legacy PIN pairing')
    code,text=pair('passkey','654321')
    check(code==0 and int(state['answer'][0])==654321,'passkey entry pairing')
    check(pair('passkey','9999999')[0]!=0,'out-of-range passkey rejected')
    code,text=pair('display')
    check(code==0 and 'DISPLAY\t987654' in text,'keyboard display passkey')
    check(pair('unknown-device','yes')[0]!=0,'agent rejects a different device')
    check(pair('authorize','no')[0]!=0,'authorization is not automatic')
    check(pair('authorize','yes')[0]==0,'selected device authorization accepted explicitly')
    check(not agent,'pairing agent unregistered after completion')
    check(run('scan')[0]==0 and not state.get('scanning',False),'bounded scan cleaned up')
    check(run('forget',address)[0]==0 and not state['exists'],'forget selected bond')
    state['present']=False
    code,text=run('list')
    check(code==0 and 'ADAPTER\t\t0' in text,'no adapter is reported honestly')
    check(run('scan')[0]!=0,'scan without adapter refused')
    print('BLUETOOTH_DBUS_OK',checks,flush=True)
finally:
    loop.quit()
