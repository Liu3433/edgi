#include "mcp_server.h"
#include "../../robot/robot.h"
#include <algorithm>
#include <cstring>
#include "../iot/thing_manager.h"
#include <webclient.h>
// #include "lwip/apps/websocket_client.h"   // 提供 wsock_write 和 OPCODE_TEXT 定义
#include "xiaozhi.h"        // 提供 g_app 定义

extern "C" {
#include "drv_es8388.h"
}

extern xiaozhi_app_t g_app;   // 声明全局应用结构体
#define TAG "MCP"
#define BOARD_NAME "psocedge84"
#define DEFAULT_TOOLCALL_STACK_SIZE 6144

McpServer::McpServer()
{
}

McpServer::~McpServer()
{
    for (auto tool : tools_)
    {
        delete tool;
    }
    tools_.clear();
}

void McpServer::AddCommonTools()
{
    // To speed up the response time, we add the common tools to the beginning of
    // the tools list to utilize the prompt cache.
    // Backup the original tools list and restore it after adding the common tools.
    auto original_tools = std::move(tools_);
#ifdef BSP_USING_ROBOT
    AddTool("self.robot.set_pose",
            "设置头部舵机角度：pan 水平20..160度，tilt 俯仰30..150度，中心90/90。"
            "用户要求指定角度时调用此工具；方向取决于安装，不能推断左右上下。"
            "Returns queued, not motion completion. Read get_status for UNO ACK. "
            "Auto expressions pause for 10 seconds after a manual request.",
            PropertyList({Property("pan", kPropertyTypeInteger, ROBOT_PAN_MIN, ROBOT_PAN_MAX),
                          Property("tilt", kPropertyTypeInteger, ROBOT_TILT_MIN, ROBOT_TILT_MAX)}),
            [](const PropertyList &p) -> ReturnValue {
                rt_err_t err = robot_set_pose(p["pan"].value<int>(), p["tilt"].value<int>());
                rt_kprintf("[robot MCP] set_pose pan=%d tilt=%d result=%d\n",
                           p["pan"].value<int>(), p["tilt"].value<int>(), err);
                if (err != RT_EOK) return ToolError{"UNO离线、未初始化或角度无效；动作未提交。请检查get_status。"};
                return std::string("queued：已提交目标，尚未收到UNO确认，不代表舵机已经到位。后续可用get_status检查。");
            });
    AddTool("self.robot.action", "控制实体头部动作：点头/点点头用nod，摇头/摇摇头用shake，"
            "回正/回中用center。用户要求动作时必须调用工具，不能仅口头答应。"
            "Returns queued, not completion; auto expressions pause for 10 seconds.",
            PropertyList({Property("action", kPropertyTypeString)}),
            [](const PropertyList &p) -> ReturnValue {
                rt_err_t err = robot_action(p["action"].value<std::string>().c_str());
                rt_kprintf("[robot MCP] action=%s result=%d\n", p["action"].value<std::string>().c_str(), err);
                if (err != RT_EOK) return ToolError{"UNO离线、未初始化或动作无效；action仅支持center/nod/shake。动作未提交。"};
                return std::string("queued：动作已提交，尚未收到UNO确认，不代表物理动作已经完成。");
            });
    AddTool("self.robot.set_auto", "开启/关闭自动头部动作、自动点头：enabled=true/false。"
            "关闭后仍可按用户要求执行点头、摇头或设置角度。"
            "Disabling holds the last target; explicit actions and heartbeat still work.",
            PropertyList({Property("enabled", kPropertyTypeBoolean)}),
            [](const PropertyList &p) -> ReturnValue {
                rt_err_t err = robot_set_auto(p["enabled"].value<bool>());
                rt_kprintf("[robot MCP] auto=%d result=%d\n", p["enabled"].value<bool>(), err);
                if (err != RT_EOK) return ToolError{"机器人模块未初始化，无法修改自动动作设置。"};
                return std::string(p["enabled"].value<bool>() ? "自动动作已开启。" : "自动动作已关闭，显式动作仍可使用。");
            });
    AddTool("self.robot.get_status", "查询舵机/头部连接状态、目标角度、UNO确认值及错误计数。"
            "ACK is not a physical position measurement. online=false means motion is unavailable.",
            PropertyList(), [](const PropertyList &) -> ReturnValue {
                robot_status_t s;
                if (robot_get_status(&s) != RT_EOK) return ToolError{"机器人模块未初始化。"};
                rt_kprintf("[robot MCP] status online=%d busy=%d ack=%d,%d\n", s.online, s.busy, s.ack_pan, s.ack_tilt);
                char out[320];
                rt_snprintf(out, sizeof(out),
                    "{\"online\":%s,\"auto\":%s,\"busy\":%s,\"pan\":%d,\"tilt\":%d,"
                    "\"ack_pan\":%d,\"ack_tilt\":%d,\"tx\":%lu,\"ack\":%lu,"
                    "\"timeouts\":%lu,\"errors\":%lu,\"bad_lines\":%lu}",
                    s.online ? "true" : "false", s.automatic ? "true" : "false", s.busy ? "true" : "false",
                    s.target_pan, s.target_tilt, s.ack_pan, s.ack_tilt,
                    (unsigned long)s.tx_count, (unsigned long)s.ack_count,
                    (unsigned long)s.timeouts, (unsigned long)s.errors, (unsigned long)s.bad_lines);
                return std::string(out);
            });
#endif
#if 1
    auto speaker = iot::ThingManager::GetInstance().GetThing("Speaker");
    if (speaker)
    {
        //设置音量工具
        AddTool("self.audio_speaker.set_volume",
                "Set the volume of the audio speaker,Must not exceed 100.",
                PropertyList(
        {
            Property("volume", kPropertyTypeInteger, 0, 100)
        }),
        [ = ](const PropertyList & properties) -> ReturnValue
        {
            int volume = properties["volume"].value<int>();
            auto json_str = R"({"method":"SetVolume","parameters":{"volume":)" + std::to_string(volume) + "}}";
            auto command = cJSON_Parse(json_str.c_str());
            if (command)
            {
                speaker->Invoke(command);
                cJSON_Delete(command); // 使用完记得释放内存
            }
            return true;
        });

        //获取当前音量工具
        AddTool("self.audio_speaker.get_volume",
                "Get the current volume of the audio speaker.",
                PropertyList(),
                [ = ](const PropertyList &) -> ReturnValue
        {
            const char *json_str = R"({"method":"GetVolume","parameters":{}})";
            cJSON *cmd = cJSON_Parse(json_str); // 直接使用 const char*
            speaker->Invoke(cmd);
            cJSON_Delete(cmd);
            return es8388_volume_get();
        });
    }

    // 屏幕控制工具
    auto screen = iot::ThingManager::GetInstance().GetThing("Screen");
    if (screen)
    {
        // 设置表情
        AddTool("self.screen.set_emoji",
                "Set emoji on screen: neutral/happy/sad/angry/etc.",
                PropertyList(
        {
            Property("emoji", kPropertyTypeString)
        }),
        [ = ](const PropertyList & properties) -> ReturnValue
        {
            std::string emoji = properties["emoji"].value<std::string>();
            auto json_str = R"({"method":"SetEmoji","parameters":{"emoji":")" + emoji + R"("}})";
            auto command = cJSON_Parse(json_str.c_str());
            if (command)
            {
                screen->Invoke(command);
                cJSON_Delete(command);
            }
            return true;
        });

        // 设置屏幕亮度
        AddTool("self.screen.set_brightness",
                "Set screen brightness 0-100.",
                PropertyList(
        {
            Property("brightness", kPropertyTypeInteger, 0, 100)
        }),
        [ = ](const PropertyList & properties) -> ReturnValue
        {
            int brightness = properties["brightness"].value<int>();
            auto json_str = R"({"method":"SetBrightness","parameters":{"brightness":)" + std::to_string(brightness) + "}}";
            auto command = cJSON_Parse(json_str.c_str());
            if (command)
            {
                screen->Invoke(command);
                cJSON_Delete(command);
            }
            return true;
        });
    }

    // LED灯控制工具 - 单一控制接口
    auto led = iot::ThingManager::GetInstance().GetThing("Led");
    if (led)
    {
        AddTool("self.led.set",
                "Control LED. color: red/green/blue to turn on single LED, red_off/green_off/blue_off to turn off single LED, all to turn on all, off to turn off all.",
                PropertyList({Property("color", kPropertyTypeString)}),
                [ = ](const PropertyList & properties) -> ReturnValue
        {
            std::string color = properties["color"].value<std::string>();
            char json_buf[64];
            snprintf(json_buf, sizeof(json_buf), R"({"method":"SetLed","parameters":{"color":"%s"}})", color.c_str());
            auto command = cJSON_Parse(json_buf);
            if (command)
            {
                led->Invoke(command);
                cJSON_Delete(command);
            }
            return true;
        });
    }
#endif
    // Restore the original tools list to the end of the tools list
    tools_.insert(tools_.end(), original_tools.begin(), original_tools.end());
}

void McpServer::SendText(const std::string &text)
{
    if (!g_app.ws.is_connected || !g_app.ws.clnt.pcb) {
        rt_kprintf("[MCP] tx skipped: websocket offline\n");
        return;
    }
    // Called from the lwIP RX callback. wsock_write already locks TCPIP core;
    // taking the audio writer mutex here can invert the core/mutex lock order.
    err_t result = wsock_write(&g_app.ws.clnt, text.c_str(), text.length(), OPCODE_TEXT);
    rt_kprintf("[MCP] tx bytes=%u result=%d\n", (unsigned)text.length(), result);
}
void McpServer::SendmcpMessage(const std::string &payload)
{
//    std::string message = "{\"session_id\":\"" + g_app.ws.session_id +
//                          "\",\"type\":\"mcp\",\"payload\":" + payload + "}";
    std::string message = "{\"session_id\":\"" + std::string(reinterpret_cast<const char *>(g_app.ws.session_id)) +
                          "\",\"type\":\"mcp\",\"payload\":" + payload + "}";
    McpServer::SendText(message);
}

void McpServer::AddTool(McpTool *tool)
{
    // Prevent adding duplicate tools
    if (std::find_if(tools_.begin(), tools_.end(), [tool](const McpTool * t)
{
    return t->name() == tool->name();
    }) != tools_.end())
    {
        rt_kprintf(TAG, "Tool %s already added", tool->name().c_str());
        delete tool;
        return;
    }

    rt_kprintf("[MCP] Add tool: %s\n", tool->name().c_str());
    tools_.push_back(tool);
}

void McpServer::AddTool(const std::string &name, const std::string &description, const PropertyList &properties, std::function<ReturnValue(const PropertyList &)> callback)
{
    AddTool(new McpTool(name, description, properties, callback));
}

void McpServer::ParseMessage(const std::string &message)
{
    cJSON *json = cJSON_Parse(message.c_str());
    if (json == nullptr)
    {
        rt_kprintf(TAG, "Failed to parse MCP message: %s", message.c_str());
        return;
    }
    ParseMessage(json);
    cJSON_Delete(json);
}

void McpServer::ParseCapabilities(const cJSON *capabilities)
{

}

void McpServer::ParseMessage(const cJSON *json)
{
    // Check JSONRPC version
    auto version = cJSON_GetObjectItem(json, "jsonrpc");
    if (version == nullptr || !cJSON_IsString(version) || strcmp(version->valuestring, "2.0") != 0)
    {
        rt_kprintf(TAG, "Invalid JSONRPC version: %s", version ? version->valuestring : "null");
        return;
    }

    // Check method
    auto method = cJSON_GetObjectItem(json, "method");
    if (method == nullptr || !cJSON_IsString(method))
    {
        rt_kprintf(TAG, "Missing method");
        return;
    }

    auto method_str = std::string(method->valuestring);
    rt_kprintf("[MCP] rx method=%s\n", method_str.c_str());
    if (method_str.find("notifications") == 0)
    {
        return;
    }

    // Check params
    auto params = cJSON_GetObjectItem(json, "params");
    if (params != nullptr && !cJSON_IsObject(params))
    {
        rt_kprintf(TAG, "Invalid params for method: %s", method_str.c_str());
        return;
    }

    auto id = cJSON_GetObjectItem(json, "id");
    if (id == nullptr || !cJSON_IsNumber(id))
    {
        rt_kprintf(TAG, "Invalid id for method: %s", method_str.c_str());
        return;
    }
    auto id_int = id->valueint;

    if (method_str == "initialize")
    {


        //auto app_desc = esp_app_get_description();
        std::string message = "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"" BOARD_NAME "\",\"version\":\"";
        message += "1.0.0";
        message += "\"}}";
        ReplyResult(id_int, message);
    }
    else if (method_str == "tools/list")
    {
        std::string cursor_str = "";
        if (params != nullptr)
        {
            auto cursor = cJSON_GetObjectItem(params, "cursor");
            if (cJSON_IsString(cursor))
            {
                cursor_str = std::string(cursor->valuestring);
            }
        }
        GetToolsList(id_int, cursor_str);
    }
    else if (method_str == "tools/call")
    {
        if (!cJSON_IsObject(params))
        {
            rt_kprintf(TAG, "tools/call: Missing params");
            ReplyError(id_int, "Missing params");
            return;
        }
        auto tool_name = cJSON_GetObjectItem(params, "name");
        if (!cJSON_IsString(tool_name))
        {
            rt_kprintf(TAG, "tools/call: Missing name");
            ReplyError(id_int, "Missing name");
            return;
        }
        auto tool_arguments = cJSON_GetObjectItem(params, "arguments");
        if (tool_arguments != nullptr && !cJSON_IsObject(tool_arguments))
        {
            rt_kprintf(TAG, "tools/call: Invalid arguments");
            ReplyError(id_int, "Invalid arguments");
            return;
        }
        auto stack_size = cJSON_GetObjectItem(params, "stackSize");
        if (stack_size != nullptr && !cJSON_IsNumber(stack_size))
        {
            rt_kprintf(TAG, "tools/call: Invalid stackSize");
            ReplyError(id_int, "Invalid stackSize");
            return;
        }
        DoToolCall(id_int, std::string(tool_name->valuestring), tool_arguments, stack_size ? stack_size->valueint : DEFAULT_TOOLCALL_STACK_SIZE);
    }
    else
    {
        rt_kprintf(TAG, "Method not implemented: %s", method_str.c_str());
        ReplyError(id_int, "Method not implemented: " + method_str);
    }
}

void McpServer::ReplyResult(int id, const std::string &result)
{
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id) + ",\"result\":";
    payload += result;
    payload += "}";
    McpServer::SendmcpMessage(payload);
    //wsock_write(&g_xz_ws.clnt, payload, strlen(payload), OPCODE_TEXT)
}
void McpServer::ReplyError(int id, const std::string &message)
{
    // User-supplied tool/argument names must be JSON escaped, not concatenated.
    cJSON *reply = cJSON_CreateObject();
    if (!reply) return;
    cJSON_AddStringToObject(reply, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(reply, "id", id);
    cJSON *error = cJSON_AddObjectToObject(reply, "error");
    cJSON_AddNumberToObject(error, "code", -32602);
    cJSON_AddStringToObject(error, "message", message.c_str());
    char *payload = cJSON_PrintUnformatted(reply);
    if (payload) {
        McpServer::SendmcpMessage(payload);
        cJSON_free(payload);
    }
    cJSON_Delete(reply);
}

void McpServer::GetToolsList(int id, const std::string &cursor)
{
    McpToolPage page = BuildMcpToolPage(tools_, cursor);
    if (!page.error.empty()) {
        rt_kprintf("[MCP] tools/list error=%s\n", page.error.c_str());
        ReplyError(id, page.error);
        return;
    }
    rt_kprintf("[MCP] tools/list total=%u page=%u bytes=%u next=%s\n",
               (unsigned)tools_.size(), (unsigned)page.count,
               (unsigned)page.json.size(), page.next_cursor.empty() ? "<end>" : page.next_cursor.c_str());
    ReplyResult(id, page.json);
}

void McpServer::DoToolCall(int id, const std::string &tool_name, const cJSON *tool_arguments, int stack_size)
{
    auto tool_iter = std::find_if(tools_.begin(), tools_.end(),
                                  [&tool_name](const McpTool * tool)
    {
        return tool->name() == tool_name;
    });

    if (tool_iter == tools_.end())
    {
        rt_kprintf(TAG, "tools/call: Unknown tool: %s", tool_name.c_str());
        ReplyError(id, "Unknown tool: " + tool_name);
        return;
    }

    PropertyList arguments = (*tool_iter)->properties();
    //try {
    for (auto &argument : arguments)
    {
        bool found = false;
        if (cJSON_IsObject(tool_arguments))
        {
            auto value = cJSON_GetObjectItem(tool_arguments, argument.name().c_str());
            if (argument.type() == kPropertyTypeBoolean && cJSON_IsBool(value))
            {
                argument.set_value<bool>(value->valueint == 1);
                found = true;
            }
            else if (argument.type() == kPropertyTypeInteger && cJSON_IsNumber(value))
            {
                if (tool_name.compare(0, 11, "self.robot.") == 0 &&
                    !argument.accepts_integer(value->valuedouble))
                {
                    rt_kprintf("[robot MCP] rejected invalid %s\n", argument.name().c_str());
                    ReplyError(id, "Expected an integer within the allowed range: " + argument.name());
                    return;
                }
                int value_int = value->valueint;
                rt_kprintf("value_int: %d\n", value_int);
                if (argument.has_range())
                {
                    value_int = std::clamp(value_int, argument.min_value(), argument.max_value());
                }

                argument.set_value<int>(value_int);
                found = true;
            }
            else if (argument.type() == kPropertyTypeString && cJSON_IsString(value))
            {
                argument.set_value<std::string>(value->valuestring);
                found = true;
            }
        }

        if (!argument.has_default_value() && !found)
        {
            rt_kprintf(TAG, "tools/call: Missing valid argument: %s", argument.name().c_str());
            ReplyError(id, "Missing valid argument: " + argument.name());
            return;
        }
    }
    // } catch (const std::runtime_error& e) {
    //     rt_kprintf(TAG, "tools/call: %s", e.what());
    //     ReplyError(id, e.what());
    //     return;
    // }

    // Start a task to receive data with stack size
    // esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    // cfg.thread_name = "tool_call";
    // cfg.stack_size = stack_size;
    // cfg.prio = 1;
    // esp_pthread_set_cfg(&cfg);

    // Use a thread to call the tool to avoid blocking the main thread
    // tool_call_thread_ = std::thread([this, id, tool_iter, args = arguments]() {
    //     try {

    //     } catch (const std::runtime_error& e) {
    //         rt_kprintf(TAG, "tools/call: %s", e.what());
    //         ReplyError(id, e.what());
    //     }
    // });
    // tool_call_thread_.detach();
    ReplyResult(id, (*tool_iter)->Call(arguments));
}
