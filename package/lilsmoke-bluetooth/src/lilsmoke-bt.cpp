// Small BlueZ D-Bus client. Pairing answers come from the foreground UI only.
#include <gio/gio.h>
#include <glib-unix.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <csignal>

namespace {
GDBusConnection *bus;
GMainLoop *loop;
GDBusMethodInvocation *pending;
std::string target, question, bluezOwner;
int result = 1;
constexpr const char *agentPath = "/org/lilsmoke/agent";
bool mac(const std::string &s) {
    if (s.size()!=17) return false;
    for (size_t i=0;i<s.size();++i)
        if (i%3==2 ? s[i]!=':' : !g_ascii_isxdigit(s[i])) return false;
    return true;
}
std::string clean(std::string s) {
    if (s.size()>160) s.resize(160);
    for (char &c:s) if (static_cast<unsigned char>(c)<32 || c==127) c=' ';
    return s;
}
GVariant *call(const char *path, const char *iface, const char *method,
               GVariant *params=nullptr, int timeout=5000) {
    GError *error=nullptr;
    auto *v=g_dbus_connection_call_sync(bus,"org.bluez",path,iface,method,
        params,nullptr,G_DBUS_CALL_FLAGS_NONE,timeout,nullptr,&error);
    if (error) { std::printf("ERROR\t%s\n",clean(error->message).c_str()); g_error_free(error); }
    return v;
}
bool invoke(const std::string &p,const char *iface,const char *method,GVariant *v=nullptr) {
    GVariant *reply=call(p.c_str(),iface,method,v);
    if (!reply) return false;
    g_variant_unref(reply); return true;
}
bool property(const std::string &p,const char *iface,const char *key,bool value) {
    return invoke(p,"org.freedesktop.DBus.Properties","Set",
        g_variant_new("(ssv)",iface,key,g_variant_new_boolean(value)));
}
struct Device { std::string path,address,name; bool paired=false,connected=false,audio=false; };
std::vector<Device> devices;
std::string adapter;
bool powered=false;
bool inventory() {
    GVariant *reply=call("/","org.freedesktop.DBus.ObjectManager","GetManagedObjects");
    if (!reply) return false;
    GVariant *objects=g_variant_get_child_value(reply,0);
    GVariantIter it; g_variant_iter_init(&it,objects);
    const char *path; GVariant *interfaces;
    while (g_variant_iter_next(&it,"{&o@a{sa{sv}}}",&path,&interfaces)) {
        auto *a=g_variant_lookup_value(interfaces,"org.bluez.Adapter1",G_VARIANT_TYPE("a{sv}"));
        if (a && adapter.empty()) {
            adapter=path; gboolean power=false;
            g_variant_lookup(a,"Powered","b",&power); powered=power;
        }
        if (a) g_variant_unref(a);
        auto *d=g_variant_lookup_value(interfaces,"org.bluez.Device1",G_VARIANT_TYPE("a{sv}"));
        if (d && devices.size()<128) {
            const char *address="", *name=""; gboolean paired=false,connected=false;
            g_variant_lookup(d,"Address","&s",&address); g_variant_lookup(d,"Alias","&s",&name);
            g_variant_lookup(d,"Paired","b",&paired); g_variant_lookup(d,"Connected","b",&connected);
            bool audio=false;
            auto *uuids=g_variant_lookup_value(d,"UUIDs",G_VARIANT_TYPE("as"));
            if (uuids) {
                GVariantIter u; g_variant_iter_init(&u,uuids); const char *uuid;
                while (g_variant_iter_next(&u,"&s",&uuid))
                    if (g_ascii_strncasecmp(uuid,"0000110b",8)==0) audio=true;
                g_variant_unref(uuids);
            }
            if (mac(address)) devices.push_back({path,address,clean(name),bool(paired),bool(connected),audio});
        }
        if (d) g_variant_unref(d);
        g_variant_unref(interfaces);
    }
    g_variant_unref(objects); g_variant_unref(reply); return true;
}
void reject() {
    if (!pending) return;
    g_dbus_method_invocation_return_dbus_error(pending,"org.bluez.Error.Rejected","User declined");
    g_object_unref(pending); pending=nullptr; question.clear();
}
gboolean answer(GIOChannel *channel,GIOCondition condition,gpointer) {
    if (condition&(G_IO_HUP|G_IO_ERR)) { reject(); g_main_loop_quit(loop); return G_SOURCE_REMOVE; }
    gchar *line=nullptr; gsize size=0;
    if (g_io_channel_read_line(channel,&line,&size,nullptr,nullptr)!=G_IO_STATUS_NORMAL) return G_SOURCE_CONTINUE;
    std::string s=line?line:""; g_free(line);
    while (!s.empty() && (s.back()=='\n'||s.back()=='\r')) s.pop_back();
    if (!pending) return G_SOURCE_CONTINUE;
    bool digits=!s.empty(); for(char c:s) if(c<'0'||c>'9') digits=false;
    if (question=="CONFIRM" && s=="yes") g_dbus_method_invocation_return_value(pending,nullptr);
    else if (question=="PIN" && !s.empty() && s.size()<=16)
        g_dbus_method_invocation_return_value(pending,g_variant_new("(s)",s.c_str()));
    else if (question=="PASSKEY" && digits && s.size()<=6)
        g_dbus_method_invocation_return_value(pending,g_variant_new("(u)",guint32(std::stoul(s))));
    else { reject(); return G_SOURCE_CONTINUE; }
    g_object_unref(pending); pending=nullptr; question.clear(); return G_SOURCE_CONTINUE;
}
void agentCall(GDBusConnection *,const gchar *sender,const gchar *,const gchar *,const gchar *method,
               GVariant *params,GDBusMethodInvocation *invocation,gpointer) {
    if (!sender || bluezOwner!=sender) {
        g_dbus_method_invocation_return_dbus_error(invocation,"org.bluez.Error.Rejected","Untrusted sender"); return;
    }
    if (!std::strcmp(method,"Release") || !std::strcmp(method,"Cancel")) {
        reject(); g_dbus_method_invocation_return_value(invocation,nullptr); return;
    }
    const char *device=nullptr;
    g_variant_get_child(params,0,"&o",&device);
    if (target!=device || pending) {
        g_dbus_method_invocation_return_dbus_error(invocation,"org.bluez.Error.Rejected","Not the selected device"); return;
    }
    if (!std::strcmp(method,"DisplayPinCode")) {
        const char *pin; g_variant_get_child(params,1,"&s",&pin);
        std::printf("DISPLAY\t%s\n",clean(pin).c_str());
        g_dbus_method_invocation_return_value(invocation,nullptr); return;
    }
    if (!std::strcmp(method,"DisplayPasskey")) {
        guint32 pass; g_variant_get_child(params,1,"u",&pass);
        std::printf("DISPLAY\t%06u\n",pass);
        g_dbus_method_invocation_return_value(invocation,nullptr); return;
    }
    std::string text;
    if (!std::strcmp(method,"RequestPinCode")) question="PIN";
    else if (!std::strcmp(method,"RequestPasskey")) question="PASSKEY";
    else if (!std::strcmp(method,"RequestConfirmation")) {
        question="CONFIRM"; guint32 pass; g_variant_get_child(params,1,"u",&pass);
        char b[16]; std::snprintf(b,sizeof(b),"%06u",pass); text=b;
    } else if (!std::strcmp(method,"RequestAuthorization") || !std::strcmp(method,"AuthorizeService")) {
        question="CONFIRM"; text="Authorize selected device";
    } else {
        g_dbus_method_invocation_return_dbus_error(invocation,"org.bluez.Error.Rejected","Unsupported request"); return;
    }
    pending=G_DBUS_METHOD_INVOCATION(g_object_ref(invocation));
    std::printf("%s\t%s\n",question.c_str(),text.c_str());
}
constexpr const char *xml=R"(<node><interface name="org.bluez.Agent1">
<method name="Release"/><method name="Cancel"/>
<method name="RequestPinCode"><arg type="o" direction="in"/><arg type="s" direction="out"/></method>
<method name="RequestPasskey"><arg type="o" direction="in"/><arg type="u" direction="out"/></method>
<method name="DisplayPinCode"><arg type="o" direction="in"/><arg type="s" direction="in"/></method>
<method name="DisplayPasskey"><arg type="o" direction="in"/><arg type="u" direction="in"/><arg type="q" direction="in"/></method>
<method name="RequestConfirmation"><arg type="o" direction="in"/><arg type="u" direction="in"/></method>
<method name="RequestAuthorization"><arg type="o" direction="in"/></method>
<method name="AuthorizeService"><arg type="o" direction="in"/><arg type="s" direction="in"/></method>
</interface></node>)";
void paired(GObject *source,GAsyncResult *async,gpointer) {
    GError *error=nullptr; auto *reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),async,&error);
    if (reply) { g_variant_unref(reply); result=0; std::puts("OK\tPaired"); }
    else { std::printf("ERROR\t%s\n",clean(error?error->message:"Pair failed").c_str()); if(error)g_error_free(error); }
    g_main_loop_quit(loop);
}
gboolean abortPair(gpointer) { reject(); g_main_loop_quit(loop); return G_SOURCE_REMOVE; }
int pairDevice() {
    GError *ownerError=nullptr;
    GVariant *owner=g_dbus_connection_call_sync(bus,"org.freedesktop.DBus","/org/freedesktop/DBus",
        "org.freedesktop.DBus","GetNameOwner",g_variant_new("(s)","org.bluez"),
        G_VARIANT_TYPE("(s)"),G_DBUS_CALL_FLAGS_NONE,3000,nullptr,&ownerError);
    if(!owner) { if(ownerError)g_error_free(ownerError); return 1; }
    const char *name; g_variant_get(owner,"(&s)",&name); bluezOwner=name; g_variant_unref(owner);
    loop=g_main_loop_new(nullptr,false);
    auto *node=g_dbus_node_info_new_for_xml(xml,nullptr);
    const GDBusInterfaceVTable table={agentCall,nullptr,nullptr,{nullptr}};
    guint registration=g_dbus_connection_register_object(bus,agentPath,node->interfaces[0],&table,nullptr,nullptr,nullptr);
    if (!registration || !invoke("/org/bluez","org.bluez.AgentManager1","RegisterAgent",
        g_variant_new("(os)",agentPath,"KeyboardDisplay"))) return 1;
    auto *channel=g_io_channel_unix_new(0);
    guint input=g_io_add_watch(channel,GIOCondition(G_IO_IN|G_IO_HUP|G_IO_ERR),answer,nullptr);
    guint timer=g_timeout_add_seconds(90,abortPair,nullptr);
    guint sig=g_unix_signal_add(SIGTERM,abortPair,nullptr);
    g_dbus_connection_call(bus,"org.bluez",target.c_str(),"org.bluez.Device1","Pair",nullptr,nullptr,
        G_DBUS_CALL_FLAGS_NONE,85000,nullptr,paired,nullptr);
    g_main_loop_run(loop);
    if(result) invoke(target,"org.bluez.Device1","CancelPairing");
    reject();
    invoke("/org/bluez","org.bluez.AgentManager1","UnregisterAgent",g_variant_new("(o)",agentPath));
    // Some sources remove themselves on HUP/timeout; destroy only live sources.
    for(guint id:{input,timer,sig}) if(g_main_context_find_source_by_id(nullptr,id)) g_source_remove(id);
    g_io_channel_unref(channel); g_dbus_connection_unregister_object(bus,registration);
    g_dbus_node_info_unref(node); g_main_loop_unref(loop);
    return result;
}
}
int main(int argc,char **argv) {
    std::setvbuf(stdout,nullptr,_IOLBF,0);
    if(argc<2) return 2;
    GError *error=nullptr; bus=g_bus_get_sync(G_BUS_TYPE_SYSTEM,nullptr,&error);
    if(!bus) { std::printf("ERROR\t%s\n",clean(error?error->message:"No system bus").c_str()); if(error)g_error_free(error); return 1; }
    if(!inventory()) return 1;
    const std::string action=argv[1];
    if(action=="list") {
        std::printf("ADAPTER\t%s\t%d\n",adapter.c_str(),powered);
        for(auto &d:devices) std::printf("DEVICE\t%s\t%s\t%d\t%d\t%d\n",d.address.c_str(),d.name.c_str(),d.paired,d.connected,d.audio);
        return 0;
    }
    if(adapter.empty()) { std::puts("ERROR\tNo supported Bluetooth adapter"); return 1; }
    if(action=="power") {
        if(argc!=3 || (std::strcmp(argv[2],"on")&&std::strcmp(argv[2],"off"))) return 2;
        return property(adapter,"org.bluez.Adapter1","Powered",!std::strcmp(argv[2],"on"))?0:1;
    }
    if(action=="scan") {
        if(!powered || !invoke(adapter,"org.bluez.Adapter1","StartDiscovery")) return 1;
        loop=g_main_loop_new(nullptr,false); g_timeout_add_seconds(10,abortPair,nullptr);
        g_unix_signal_add(SIGTERM,abortPair,nullptr); g_main_loop_run(loop);
        invoke(adapter,"org.bluez.Adapter1","StopDiscovery"); g_main_loop_unref(loop); return 0;
    }
    if(argc!=3 || !mac(argv[2])) return 2;
    Device *selected=nullptr;
    for(auto &d:devices) if(g_ascii_strcasecmp(d.address.c_str(),argv[2])==0) selected=&d;
    if(!selected) { std::puts("ERROR\tDevice not found"); return 1; }
    target=selected->path;
    if(action=="pair") return pairDevice();
    if(action=="trust" || action=="untrust") {
        if(!selected->paired) { std::puts("ERROR\tPair the device first"); return 1; }
        return property(target,"org.bluez.Device1","Trusted",action=="trust")?0:1;
    }
    if(action=="connect") return invoke(target,"org.bluez.Device1","Connect")?0:1;
    if(action=="disconnect") return invoke(target,"org.bluez.Device1","Disconnect")?0:1;
    if(action=="forget") return invoke(adapter,"org.bluez.Adapter1","RemoveDevice",g_variant_new("(o)",target.c_str()))?0:1;
    if(action=="audio") {
        if(!selected->connected || !selected->audio) return 1;
        std::printf("bluealsa:DEV=%s,PROFILE=a2dp\n",selected->address.c_str()); return 0;
    }
    return 2;
}
