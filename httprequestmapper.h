#ifndef HTTPREQUESTMAPPER_H
#define HTTPREQUESTMAPPER_H

#include <httprequesthandler.h>

using namespace stefanfrings;

class httprequestmapper : public HttpRequestHandler
{
    Q_OBJECT
    Q_DISABLE_COPY(httprequestmapper)
public:
    /**
      Constructor.
      @param parent Parent object
    */
    httprequestmapper(QObject* parent=0);

    /**
      Destructor.
    */
    ~httprequestmapper();

    /**
      Dispatch incoming HTTP requests to different controllers depending on the URL.
      @param request The received HTTP request
      @param response Must be used to return the response
    */
    void service(HttpRequest& request, HttpResponse& response);
};

#endif // HTTPREQUESTMAPPER_H
