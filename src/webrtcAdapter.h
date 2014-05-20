#pragma once
#include <talk/base/common.h>
#include <talk/base/scoped_ref_ptr.h>
#include <talk/app/webrtc/peerconnectioninterface.h>
#include <talk/app/webrtc/jsep.h>
#include <talk/app/webrtc/peerconnectionfactory.h>
#include <talk/app/webrtc/mediastream.h>
#include <talk/app/webrtc/audiotrack.h>
#include <talk/app/webrtc/videotrack.h>
#include <talk/app/webrtc/test/fakeconstraints.h>
#include <talk/app/webrtc/jsepsessiondescription.h>
#include <talk/app/webrtc/jsep.h>
#include "guiCallMarshaller.h"
#include "promise.h"

namespace rtcModule
{
/** Global PeerConnectionFactory that initializes and holds a webrtc runtime context*/

extern talk_base::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
 gWebrtcContext;
struct Identity
{
	std::string derCert;
	std::string derPrivateKey;
	inline void clear()
	{
		derCert.clear();
		derPrivateKey.clear();
	}
	inline bool isValid() {return !derCert.empty();}
};
/** Local DTLS SRTP identity */
extern Identity gLocalIdentity;

void init(const Identity* identity);
void cleanup();

unsigned long generateId();
/** Globally initializes the library */
/** The library's local handler for all function call marshal messages.
 * This handler is given for all marshal requests done from this
 * library(via rtcModule::marshalCall()
 * The code of this function must always be in the same dynamic library that
 * marshals requests, i.e. we need this local handler for this library
 * This function can't be inlined
 */
void funcCallMarshalHandler(mega::Message* msg);
static inline void marshalCall(std::function<void()>&& lambda)
{
	mega::marshalCall(::rtcModule::funcCallMarshalHandler,
		std::forward<std::function<void()> >(lambda));
}
typedef talk_base::scoped_refptr<webrtc::MediaStreamInterface> tspMediaStream;
typedef talk_base::scoped_refptr<webrtc::SessionDescriptionInterface> tspSdp;
typedef std::unique_ptr<webrtc::SessionDescriptionInterface> supSdp;


class SdpCreateCallbacks: public webrtc::CreateSessionDescriptionObserver
{
public:
  // The implementation of the CreateSessionDescriptionObserver takes
  // the ownership of the |desc|.
    typedef promise::Promise<webrtc::SessionDescriptionInterface*> PromiseType;
	SdpCreateCallbacks(const PromiseType& promise)
		:mPromise(promise){}
	virtual void OnSuccess(webrtc::SessionDescriptionInterface* desc)
    {
        ::rtcModule::marshalCall([this, desc]() mutable
		{
            mPromise.resolve(desc);
            Release();
		});
	}
	virtual void OnFailure(const std::string& error)
	{
		::rtcModule::marshalCall([this, error]()
		{
		   mPromise.reject(error);
           Release();
		});
	}
protected:
	PromiseType mPromise;
};

class SdpSetCallbacks: public webrtc::SetSessionDescriptionObserver
{
public:
    struct SdpText
    {
        std::string sdp;
        std::string type;
        SdpText(webrtc::SessionDescriptionInterface* desc)
        {
            type = desc->type();
            desc->ToString(&sdp);
        }
    };
    typedef promise::Promise<std::shared_ptr<SdpText> > PromiseType;
    SdpSetCallbacks(const PromiseType& promise, webrtc::SessionDescriptionInterface* sdp)
    :mPromise(promise), mSdpText(new SdpText(sdp))
    {}

	virtual void OnSuccess()
	{
		 ::rtcModule::marshalCall([this]()
		 {
             mPromise.resolve(mSdpText);
             Release();
		 });
	}
	virtual void OnFailure(const std::string& error)
	{
		::rtcModule::marshalCall([this, error]()
		{
			 mPromise.reject(error);
             Release();
		});
	}
protected:
	PromiseType mPromise;
    std::shared_ptr<SdpText> mSdpText;
};

typedef std::shared_ptr<SdpSetCallbacks::SdpText> sspSdpText;

struct StatsCallbacks: public webrtc::StatsObserver
{
	typedef promise::Promise<std::shared_ptr<std::vector<webrtc::StatsReport> > > PromiseType;
	StatsCallbacks(const PromiseType& promise):mPromise(promise){}
	virtual void OnComplete(const std::vector<webrtc::StatsReport>& reports)
	{
		PromiseType::Type stats(new std::vector<webrtc::StatsReport>(reports)); //this is a std::shared_ptr
		marshalCall([this, stats]()
		{
			mPromise.resolve(stats);
			delete this;
		});
	}
protected:
	PromiseType mPromise;
};

template <class C>
class myPeerConnection: public
		talk_base::scoped_refptr<webrtc::PeerConnectionInterface>
{
protected:

//PeerConnectionObserver implementation
  struct Observer: public webrtc::PeerConnectionObserver
  {
      Observer(C& handler):mHandler(handler){}
      virtual void OnError()
      { marshalCall([this](){mHandler.onError();}); }
      virtual void OnAddStream(webrtc::MediaStreamInterface* stream)
      {
          tspMediaStream spStream(stream);
          marshalCall([this, spStream] {mHandler.onAddStream(spStream);} );
      }
      virtual void OnRemoveStream(webrtc::MediaStreamInterface* stream)
      {
          tspMediaStream spStream(stream);
          marshalCall([this, spStream] {mHandler.onRemoveStream(spStream);} );
      }
      virtual void OnIceCandidate(const webrtc::IceCandidateInterface* candidate)
      {
         std::string sdp;
         if (!candidate->ToString(&sdp))
         {
             printf("ERROR: Failed to serialize candidate\n");
             return;
         }
         std::shared_ptr<std::string> strCand(new std::string);
         (*strCand)
          .append("candidate: ").append(sdp).append("\r\n")
          .append("sdpMid: ").append(candidate->sdp_mid()).append("\r\n")
          .append("sdpMLineIndex: ").append(std::to_string(candidate->sdp_mline_index())).append("\r\n");

         marshalCall([this, strCand](){ mHandler.onIceCandidate(*strCand); });
     }
     virtual void OnIceComplete()
     {
         marshalCall([this]() { mHandler.onIceComplete(); });
     }
     virtual void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState newState)
     {
         marshalCall([this, newState]() { mHandler.onSignalingChange(newState); });
     }
     virtual void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState newState)
     {
         marshalCall([this, newState]() { mHandler.onIceConnectionChange(newState);	});
     }
     virtual void OnRenegotiationNeeded()
     {
         marshalCall([this]() { mHandler.onRenegotiationNeeded();});
     }
    protected:
        C& mHandler;
        //own callback interface, always called by the GUI thread
    };
	typedef talk_base::scoped_refptr<webrtc::PeerConnectionInterface> Base;
	std::shared_ptr<Observer> mObserver;
public:
	myPeerConnection(const webrtc::PeerConnectionInterface::IceServers& servers,
     C& handler, webrtc::MediaConstraintsInterface* options)
        :mObserver(new Observer(handler))
	{

		if (gLocalIdentity.isValid())
		{
//TODO: give dtls identity to webrtc
		}
        Base::operator=(gWebrtcContext->CreatePeerConnection(
			servers, options, NULL, NULL /*DTLS stuff*/, mObserver.get()));
	}

  SdpCreateCallbacks::PromiseType createOffer(const webrtc::MediaConstraintsInterface* constraints)
  {
	  SdpCreateCallbacks::PromiseType promise;
      auto observer = new talk_base::RefCountedObject<SdpCreateCallbacks>(promise);
      observer->AddRef();
      get()->CreateOffer(observer, constraints);
	  return promise;
  }
  SdpCreateCallbacks::PromiseType createAnswer(const webrtc::MediaConstraintsInterface* constraints)
  {
	  SdpCreateCallbacks::PromiseType promise;
      auto observer = new talk_base::RefCountedObject<SdpCreateCallbacks>(promise);
      observer->AddRef();
      get()->CreateAnswer(observer, constraints);
	  return promise;
  }
  /** Takes ownership of \c desc */
  SdpSetCallbacks::PromiseType setLocalDescription(webrtc::SessionDescriptionInterface* desc)
  {
	  SdpSetCallbacks::PromiseType promise;
      auto observer = new talk_base::RefCountedObject<SdpSetCallbacks>(promise, desc);
      observer->AddRef();
      get()->SetLocalDescription(observer, desc);
	  return promise;
  }
  SdpSetCallbacks::PromiseType setRemoteDescription(webrtc::SessionDescriptionInterface* desc)
  {
	  SdpSetCallbacks::PromiseType promise;
      auto observer = new talk_base::RefCountedObject<SdpSetCallbacks>(promise, desc);
      observer->AddRef();
      get()->SetRemoteDescription(observer, desc);
	  return promise;
  }
  StatsCallbacks::PromiseType getStats(
	webrtc::MediaStreamTrackInterface* track, webrtc::PeerConnectionInterface::StatsOutputLevel level)
  {
	  StatsCallbacks::PromiseType promise;
	  get()->GetStats(new talk_base::RefCountedObject<StatsCallbacks>(promise), track, level);
	  return promise;
  }
};

talk_base::scoped_refptr<webrtc::MediaStreamInterface> cloneMediaStream(
		webrtc::MediaStreamInterface* other, const std::string& label);

struct DeviceManager: public
		std::shared_ptr<cricket::DeviceManagerInterface>
{
	typedef std::shared_ptr<cricket::DeviceManagerInterface> Base;
	DeviceManager()
		:Base(cricket::DeviceManagerFactory::Create())
	{
		if (!get()->Init())
		{
			reset();
			throw std::runtime_error("Can't create device manager");
		}
	}
	DeviceManager(const DeviceManager& other)
	:Base(other){}
};

typedef std::vector<cricket::Device> DeviceList;

struct InputDevices
{
	DeviceList audio;
	DeviceList video;
};

std::shared_ptr<InputDevices> getInputDevices(DeviceManager devMgr);
struct MediaGetOptions
{
	cricket::Device* device;
	webrtc::FakeConstraints constraints;
	MediaGetOptions(cricket::Device* aDevice)
	:device(aDevice){}
};

talk_base::scoped_refptr<webrtc::AudioTrackInterface>
	getUserAudio(const MediaGetOptions& options, DeviceManager& devMgr);

talk_base::scoped_refptr<webrtc::VideoTrackInterface>
	getUserVideo(const MediaGetOptions& options, DeviceManager& devMgr);

inline webrtc::JsepSessionDescription* parseSdp(const sspSdpText& sdpText)
{
    webrtc::JsepSessionDescription* sdp =
        new webrtc::JsepSessionDescription(sdpText->type);
    webrtc::SdpParseError error;
    if (!sdp->Initialize(sdpText->sdp, &error))
    {
        delete sdp;
        throw std::runtime_error(error.line+":"+error.description);
    }
    return sdp;
}
}
