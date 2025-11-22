#ifdef ENABLE_API

#include <drogon/HttpController.h>
#include <drogon/drogon.h>
#include "api/api_vm_adapter.h"

using namespace drogon;

class ApiController : public drogon::HttpController<ApiController> {
public:
    METHOD_LIST_BEGIN
    // POST /api/command
    ADD_METHOD_TO(ApiController::command, "/api/command", Post);
    // GET /api/state
    ADD_METHOD_TO(ApiController::getState, "/api/state", Get);
    METHOD_LIST_END

    void command(const HttpRequestPtr &req, std::function<void (const HttpResponsePtr &)> &&callback) {
        Json::Value resp;
        try {
            auto json = req->getJsonObject();
            if (!json || !(*json).isMember("cmd")) {
                resp["ok"] = false;
                resp["error"] = "missing cmd";
                callback(HttpResponse::newHttpJsonResponse(resp));
                return;
            }
            std::string cmd = (*json)["cmd"].asString();
            if (cmd == "step") {
                bool ok = ApiVmAdapter::Instance().Step();
                resp["ok"] = ok;
            } else if (cmd == "run") {
                bool ok = ApiVmAdapter::Instance().Run();
                resp["ok"] = ok;
            } else if (cmd == "pause") {
                bool ok = ApiVmAdapter::Instance().Pause();
                resp["ok"] = ok;
            } else if (cmd == "stop") {
                bool ok = ApiVmAdapter::Instance().Stop();
                resp["ok"] = ok;
            } else if (cmd == "undo") {
                bool ok = ApiVmAdapter::Instance().Undo();
                resp["ok"] = ok;
            } else if (cmd == "redo") {
                bool ok = ApiVmAdapter::Instance().Redo();
                resp["ok"] = ok;
            } else {
                resp["ok"] = false;
                resp["error"] = "unknown command";
            }
        } catch (const std::exception &e) {
            resp["ok"] = false;
            resp["error"] = e.what();
        }
        callback(HttpResponse::newHttpJsonResponse(resp));
    }

    void getState(const HttpRequestPtr &req, std::function<void (const HttpResponsePtr &)> &&callback) {
        std::string json = ApiVmAdapter::Instance().GetStateJson();
        auto resp = HttpResponse::newHttpResponse();
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        resp->setBody(json);
        callback(resp);
    }
};

#endif // ENABLE_API
