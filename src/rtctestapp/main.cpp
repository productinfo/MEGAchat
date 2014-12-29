#ifdef _WIN32
    #include <winsock2.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <mstrophepp.h>
#include <event2/event.h>

//#include "webrtc/base/ssladapter.h"

#include <QtGui/QApplication>
#include "mainwindow.h"
#include "../base/gcm.h"
#include "../IRtcModule.h"
#include "../lib.h"
#include "../DummyCrypto.h"
#include "../strophe.disco.h"
#include "../base/services.h"
#include "sdkApi.h"
#include "MegaCryptoFunctions.h"

const std::string jidDomain = "@developers.mega.co.nz";

using namespace std;
using namespace promise;
using namespace mega;

MainWindow* mainWin = NULL;
rtcModule::IRtcModule* rtc = NULL;
unique_ptr<rtcModule::ICryptoFunctions> crypto;
unique_ptr<rtcModule::IEventHandler> handler;
unique_ptr<MyMegaApi> api(new MyMegaApi("sdfsdfsdf"));

struct GcmEvent: public QEvent
{
    static const QEvent::Type type;
    void* ptr;
    GcmEvent(void* aPtr): QEvent(type), ptr(aPtr){}
};
const QEvent::Type GcmEvent::type = (QEvent::Type)QEvent::registerEventType();

class AppDelegate: public QObject
{
    Q_OBJECT
public slots:
    void onAppTerminate();
public:
    virtual bool event(QEvent* event)
    {
        if (event->type() != GcmEvent::type)
            return false;

        megaProcessMessage(static_cast<GcmEvent*>(event)->ptr);
        return true;
    }
};

AppDelegate appDelegate;

MEGA_GCM_EXPORT void megaPostMessageToGui(void* msg)
{
    QEvent* event = new GcmEvent(msg);
    QApplication::postEvent(&appDelegate, event);
}

using namespace strophe;


void sigintHandler(int)
{
    printf("SIGINT Received\n");
    fflush(stdout);
    mega::marshallCall([]{mainWin->close();});
}

auto message_handler = [](Stanza s, void* userdata, bool& keep)
{
    strophe::Connection& conn = *static_cast<strophe::Connection*>(userdata);
    xmpp_stanza_t* rawBody = s.rawChild("body");
    if (!rawBody)
        return;
    Stanza body(rawBody);
    if (!strcmp(s.attr("type"), "error")) return;

    auto intext = body.text();

    printf("Incoming message from %s: %s\n", s.attr("from"), intext.c_str());

    Stanza reply(conn);
    reply.init("message", {
        {"type", s.attrDefault("type", "chat")},
        {"to", s.attr("from")}
    })
    .c("body", {})
    .t(intext.c_str()+std::string(" to you too!"));

    conn.send(reply);
};

int ping(xmpp_conn_t * const pconn, void * const userdata)
{
   strophe::Connection& conn = *static_cast<strophe::Connection*>(userdata);
   Stanza ping(conn);
   ping.setName("iq")
       .setAttr("type", "get")
       .setAttr("from", conn.jid())
       .c("ping", {{"xmlns", "urn:xmpp:ping"}});
   conn.sendIqQuery(ping, "set")
   .then([](Stanza pong)
   {
         return 0;
   })
   .fail([](const promise::Error& err)
   {
       printf("Error receiving pong\n");
       return 0;
   });
   return 1;
}

const char* usermail;
const char* pass = NULL;
std::string jid;
bool inCall = false;

int main(int argc, char **argv)
{
    /* take a jid and password on the command line */
    if (argc != 4)
    {
        fprintf(stderr, "Usage: rtctestapp <usermail> <userpass> <peermail>\n\n");
        return 1;
    }
    usermail = argv[1];
    pass = argv[2];
    QApplication a(argc, argv);
    mainWin = new MainWindow;
    mainWin->ui->callBtn->setEnabled(false);
    mainWin->ui->callBtn->setText("Login...");
    QObject::connect(qApp, SIGNAL(lastWindowClosed()), &appDelegate, SLOT(onAppTerminate()));

    services_init(megaPostMessageToGui, SVC_STROPHE_LOG);

    /* create a connection */
    mainWin->mConn.reset(new strophe::Connection(services_strophe_get_ctx()));
    mainWin->ui->calleeInput->setText(argv[3]);

//get xmpp login from Mega API
    api->call(&MegaApi::login, usermail, pass)
    .then([](ReqResult result)
    {
        printf("login success\n");
        return api->call(&MegaApi::getUserData);
    })
    .then([](ReqResult result)
    {
        api->userData = result;
        const char* user = result->getText();
        if (!user || !user[0])
            throw std::runtime_error("Could not get our own JID");
        SdkString xmppPass = api->dumpXMPPSession();
        if (xmppPass.size() < 16)
            return promise::reject<int>("Mega session id is shorter than 16 bytes");
        ((char&)xmppPass.c_str()[16]) = 0;

        Connection& conn = *(mainWin->mConn.get());
        /* setup authentication information */
        jid = (user+jidDomain);
        xmpp_conn_set_jid(conn, jid.c_str());
        xmpp_conn_set_pass(conn, xmppPass.c_str());
        printf("user = '%s', pass = '%s'\n", jid.c_str(), xmppPass.c_str());

        conn.registerPlugin("disco", new disco::DiscoPlugin(conn, "Karere"));
        handler.reset(new RtcEventHandler(mainWin));

    /* create rtcModule */
        crypto.reset(new rtcModule::MegaCryptoFuncs(*api));
//        crypto.reset(new rtcModule::DummyCrypto(jid.c_str()));

        rtc = createRtcModule(conn, handler.get(), crypto.get(), "");
        rtcModule::IPtr<rtcModule::IDeviceList> audio(rtc->getAudioInDevices());
        for (size_t i=0, len=audio->size(); i<len; i++)
            mainWin->ui->audioInCombo->addItem(audio->name(i).c_str());
        rtcModule::IPtr<rtcModule::IDeviceList> video(rtc->getVideoInDevices());
        for (size_t i=0, len=video->size(); i<len; i++)
            mainWin->ui->videoInCombo->addItem(video->name(i).c_str());

        rtc->updateIceServers("url=turn:j100.server.lu:3591?transport=udp, user=alex, pass=alexsecret");
        conn.registerPlugin("rtcmodule", rtc);
        /* initiate connection */
        return conn.connect("karere-001.developers.mega.co.nz", 0);
    })
    .then([&](int)
    {
        printf("==========Connect promise resolved\n");
        mainWin->ui->callBtn->setEnabled(true);
        mainWin->ui->callBtn->setText("Call");
        Connection& conn = *(mainWin->mConn.get());
        xmpp_timed_handler_add(conn, ping, 100000, &conn);
        conn.addHandler(message_handler, NULL, "message", NULL, NULL, &conn);
    /* Send initial <presence/> so that we appear online to contacts */
        Stanza pres(conn);
        pres.setName("presence");
        conn.send(pres);
        return 0;
    })
    .fail([](const promise::Error& error)
    {
        printf("==========Connect promise failed:\n%s\n", error.msg().c_str());
        return error;
    });
    signal(SIGINT, sigintHandler);
    mainWin->show();

    return a.exec();
}

void AppDelegate::onAppTerminate()
{
    printf("onAppTerminate\n");
    rtc->destroy();
    rtcCleanup();
    services_shutdown();
    mainWin->mConn.reset();
}
#include <main.moc>
