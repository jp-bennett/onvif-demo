#include "soapPullPointSubscriptionBindingProxy.h"
#include "plugin/wsseapi.h"
#include "wsdd.nsmap"
#include <cstring>
#include <sstream>
#include <csignal>

#define USERNAME "admin"
#define PASSWORD "Password"


//add cli option handling

//add password prompt

//add verbosity flag

struct soap *soap = nullptr;
PullPointSubscriptionBindingProxy proxyEvent;
std::string subscription_endpoint;

void signalHandler(int signum) {
  std::cout << "\nInterrupted, cleaning up and closing." << std::endl;

  _wsnt__Unsubscribe wsnt__Unsubscribe;
  _wsnt__UnsubscribeResponse wsnt__UnsubscribeResponse;
  proxyEvent.Unsubscribe(subscription_endpoint.c_str(), NULL, &wsnt__Unsubscribe, wsnt__UnsubscribeResponse);
  // free all deserialized and managed data, we can still reuse the context and proxies after this
  soap_destroy(soap);
  soap_end(soap);
  soap_free(soap);

  exit(signum);
}
// to report an error
void report_error(struct soap *soap)
{
  std::cerr << "Oops, something went wrong:" << std::endl;
}

// to set the timestamp and authentication credentials in a request message
void set_credentials(struct soap *soap)
{
  soap_wsse_delete_Security(soap);
  if (soap_wsse_add_Timestamp(soap, "Time", 10)
   || soap_wsse_add_UsernameTokenDigest(soap, "Auth", USERNAME, PASSWORD))
    report_error(soap);
}


int main()
{
signal(SIGINT, signalHandler);
  // create a context with strict XML validation and exclusive XML canonicalization for WS-Security enabled
  soap = soap_new();
  soap->connect_timeout = soap->recv_timeout = soap->send_timeout = 10; // 10 sec
  soap_register_plugin(soap, soap_wsse);

  proxyEvent = PullPointSubscriptionBindingProxy(soap);
  proxyEvent.soap_endpoint = "http://10.0.1.187/onvif/Events";
  set_credentials(soap);
  _tev__CreatePullPointSubscription request;
  _tev__CreatePullPointSubscriptionResponse response;
  if (proxyEvent.CreatePullPointSubscription(&request, response) != SOAP_OK) {
    soap_stream_fault(soap, std::cerr);
    return -1;
  } else {
    std::cout << "Termination time " << response.wsnt__TerminationTime << std::endl;
    std::cout << "Current time " << response.wsnt__CurrentTime << std::endl;
    subscription_endpoint = response.SubscriptionReference.Address;
  }

  _tev__PullMessages tev__PullMessages;
  _tev__PullMessagesResponse tev__PullMessagesResponse;
  tev__PullMessages.Timeout = "PT900S";
  tev__PullMessages.MessageLimit = 100;
  //Empty the stored messages
  set_credentials(soap);
  if (proxyEvent.PullMessages(subscription_endpoint.c_str(), NULL, &tev__PullMessages, tev__PullMessagesResponse) != SOAP_OK) {
    soap_stream_fault(soap, std::cerr);
    return -1;
  }
  for(int i=0; i< 10; i++){
    set_credentials(soap);
    if (proxyEvent.PullMessages(subscription_endpoint.c_str(), NULL, &tev__PullMessages, tev__PullMessagesResponse) != SOAP_OK) {
      soap_stream_fault(soap, std::cerr);
    }
    for (auto msg : tev__PullMessagesResponse.wsnt__NotificationMessage) {
      if (msg->Topic->__any.text != NULL &&
      std::strstr(msg->Topic->__any.text, "MotionAlarm") &&
      msg->Message.__any.elts != NULL &&
      msg->Message.__any.elts->next != NULL &&
      msg->Message.__any.elts->next->elts != NULL &&
      msg->Message.__any.elts->next->elts->atts != NULL &&
      msg->Message.__any.elts->next->elts->atts->next != NULL &&
      msg->Message.__any.elts->next->elts->atts->next->text != NULL) {
        if (strcmp(msg->Message.__any.elts->next->elts->atts->next->text, "true") == 0) {
          std::cout << msg->Message.__any.elts->next->elts->atts->text << std::endl;
          std::stringstream ss;
          soap->os = &ss; // assign a stringstream to write output to
          soap_write__tev__PullMessagesResponse(soap, &tev__PullMessagesResponse);
          soap->os = NULL; // no longer writing to the stream
          std::cout << "The XML is:\n" << ss.str() << "\n\n";

        } else {

          std::cout << " Event End " << std::endl;
          std::stringstream ss;
          soap->os = &ss; // assign a stringstream to write output to
          soap_write__tev__PullMessagesResponse(soap, &tev__PullMessagesResponse);
          soap->os = NULL; // no longer writing to the stream
          std::cout << "The XML is:\n" << ss.str() << "\n\n";
        }
      }
    }
  }
  _wsnt__Unsubscribe wsnt__Unsubscribe;
  _wsnt__UnsubscribeResponse wsnt__UnsubscribeResponse;
  proxyEvent.Unsubscribe(subscription_endpoint.c_str(), NULL, &wsnt__Unsubscribe, wsnt__UnsubscribeResponse);
  // free all deserialized and managed data, we can still reuse the context and proxies after this
  soap_destroy(soap);
  soap_end(soap);
  soap_free(soap);
  return 0;
}

int SOAP_ENV__Fault(struct soap *soap, char *faultcode, char *faultstring, char *faultactor, struct SOAP_ENV__Detail *detail, struct SOAP_ENV__Code *SOAP_ENV__Code, struct SOAP_ENV__Reason *SOAP_ENV__Reason, char *SOAP_ENV__Node, char *SOAP_ENV__Role, struct SOAP_ENV__Detail *SOAP_ENV__Detail)
{
  // populate the fault struct from the operation arguments to print it
  soap_fault(soap);
  // SOAP 1.1
  soap->fault->faultcode = faultcode;
  soap->fault->faultstring = faultstring;
  soap->fault->faultactor = faultactor;
  soap->fault->detail = detail;
  // SOAP 1.2
  soap->fault->SOAP_ENV__Code = SOAP_ENV__Code;
  soap->fault->SOAP_ENV__Reason = SOAP_ENV__Reason;
  soap->fault->SOAP_ENV__Node = SOAP_ENV__Node;
  soap->fault->SOAP_ENV__Role = SOAP_ENV__Role;
  soap->fault->SOAP_ENV__Detail = SOAP_ENV__Detail;
  // set error
  soap->error = SOAP_FAULT;
  // handle or display the fault here with soap_stream_fault(soap, std::cerr);
  // return HTTP 202 Accepted
  return soap_send_empty_response(soap, SOAP_OK);
}
