#include "soapPullPointSubscriptionBindingProxy.h"
#include "plugin/wsseapi.h"
#include "wsdd.nsmap"
#include <cstring>
#include <sstream>
#include <csignal>
#include <termios.h>
#include <unistd.h>

#define USERNAME "admin"


//add verbosity flag

struct soap *soap = nullptr;
PullPointSubscriptionBindingProxy proxyEvent;
std::string subscription_endpoint;
std::string password;
std::string username;
std::string onvif_url;
std::stringstream ss;
bool verboseFlag = false;

void signalHandler(int signum) {
  std::cout << "\nInterrupted, cleaning up and closing." << std::endl;

  if (!subscription_endpoint.empty()) {
    _wsnt__Unsubscribe wsnt__Unsubscribe;
    _wsnt__UnsubscribeResponse wsnt__UnsubscribeResponse;
    proxyEvent.Unsubscribe(subscription_endpoint.c_str(), NULL, &wsnt__Unsubscribe, wsnt__UnsubscribeResponse);
  }
  if (soap) {
    soap_destroy(soap);
    soap_end(soap);
    soap_free(soap);
  }

  struct termios tty;
  tcgetattr(STDIN_FILENO, &tty);
  tty.c_lflag |= ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &tty);

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
   || soap_wsse_add_UsernameTokenDigest(soap, "Auth", username.c_str(), password.c_str()))
    report_error(soap);
}

void usage() {
  std::cerr << "Usage:" << std::endl;
  std::cerr << "onvif-demo [onvif-url] -u [username]" << std::endl;
  std::cerr << "-v verbose mode" << std::endl;
  std::cerr << "Example: onvif-demo http://10.0.1.187/onvif -u admin" << std::endl;
  exit(1);
}

int main(int argc, char** argv)
{
  int c;
  signal(SIGINT, signalHandler);


  while ((c = getopt(argc, argv, "vhu:")) != -1)
    switch (c) {
      case 'v':
        verboseFlag = true;
        break;
      case 'u':
        username = optarg;
        break;
      case 'h':
        usage();
        break;
    }
  if (optind >= argc) usage();
  onvif_url = argv[optind];
  onvif_url += "/Events";
  //check for valid config
  if (username.empty() || onvif_url.empty()) usage();

  std::cout << "Password: ";
  struct termios tty; //get password without echo, https://stackoverflow.com/questions/1413445/reading-a-password-from-stdcin
  tcgetattr(STDIN_FILENO, &tty);
  tty.c_lflag &= ~ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &tty);
  std::cin >> password;
  tty.c_lflag |= ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &tty);
  std::cout << std::endl;

  soap = soap_new();
  soap->connect_timeout = soap->recv_timeout = soap->send_timeout = 10; // 10 sec
  soap_register_plugin(soap, soap_wsse);

  proxyEvent = PullPointSubscriptionBindingProxy(soap);
  proxyEvent.soap_endpoint = onvif_url.c_str();
  set_credentials(soap);
  _tev__CreatePullPointSubscription request;
  _tev__CreatePullPointSubscriptionResponse response;
  if (proxyEvent.CreatePullPointSubscription(&request, response) != SOAP_OK) {
    std::cout << "Subscription Failed:" << std::endl;
    soap_stream_fault(soap, std::cerr);
    return -1;
  } else {
    std::cout << "Subscription successful!" << std::endl;
    subscription_endpoint = response.SubscriptionReference.Address;
    if (verboseFlag) {
      std::cout << "Termination time " << response.wsnt__TerminationTime << std::endl;
      std::cout << "Current time " << response.wsnt__CurrentTime << std::endl;
    }
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
  if (verboseFlag) {
    soap->os = &ss; // assign a stringstream to write output to
    soap_write__tev__PullMessagesResponse(soap, &tev__PullMessagesResponse);
    soap->os = NULL; // no longer writing to the stream
    std::cout << "The XML is:\n" << ss.str() << "\n\n";
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
          if (verboseFlag) {
            soap->os = &ss; // assign a stringstream to write output to
            soap_write__tev__PullMessagesResponse(soap, &tev__PullMessagesResponse);
            soap->os = NULL; // no longer writing to the stream
            std::cout << "The XML is:\n" << ss.str() << "\n\n";
          }

        } else {
          std::cout << " Event End " << std::endl;
          if (verboseFlag) {
            soap->os = &ss; // assign a stringstream to write output to
            soap_write__tev__PullMessagesResponse(soap, &tev__PullMessagesResponse);
            soap->os = NULL; // no longer writing to the stream
            std::cout << "The XML is:\n" << ss.str() << "\n\n";
          }
        }
      } else { //not a subscribe message
        if (verboseFlag) {
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
