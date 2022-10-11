#ifndef INDEXCTRL_H
#define INDEXCTRL_H

#include "httprequest.h"
#include "httpresponse.h"
#include "httprequesthandler.h"

using namespace stefanfrings;

class IndexCtrl : public HttpRequestHandler
{
    Q_OBJECT
    Q_DISABLE_COPY(IndexCtrl)
public:

    /** Constructor */
    IndexCtrl();

    /** Generates the response */
    void service(HttpRequest& request, HttpResponse& response);
    void serviceBackupLog(HttpRequest& request, HttpResponse& response);
};

#endif // INDEXCTRL_H
