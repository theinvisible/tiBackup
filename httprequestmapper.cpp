#include <QCoreApplication>
#include <httpctrl/indexctrl.h>
#include "global.h"
#include "httprequestmapper.h"
#include "staticfilecontroller.h"

httprequestmapper::httprequestmapper(QObject *parent)
    : HttpRequestHandler{parent}
{

}

httprequestmapper::~httprequestmapper()
{

}

void httprequestmapper::service(HttpRequest &request, HttpResponse &response)
{
    QByteArray path=request.getPath();
    qDebug("RequestMapper: path=%s",path.data());

    if(path == "/" || path.startsWith("/index.html")) {
        IndexCtrl().service(request, response);
    } else if(path.startsWith("/backuplog")) {
        IndexCtrl().serviceBackupLog(request, response);
    } else {
        staticFileController->service(request, response);
    }

    qDebug("RequestMapper: finished request");
}
