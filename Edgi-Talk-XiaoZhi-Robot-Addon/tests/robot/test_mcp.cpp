// Exercise the production MCP result serializer and integer validation.
#include "../../applications/xiaozhi/mcp/mcp_server.h"
#include <cassert>
#include <limits>
#include "../../applications/xiaozhi/xiaozhi_session.h"

static void check_result(ReturnValue value, bool error, const char *expected)
{
    McpTool tool("test", "test", PropertyList(),
                 [value](const PropertyList &) -> ReturnValue { return value; });
    cJSON *result = cJSON_Parse(tool.Call(PropertyList()).c_str());
    assert(result);
    assert((cJSON_IsTrue(cJSON_GetObjectItem(result, "isError")) != 0) == error);
    cJSON *content = cJSON_GetArrayItem(cJSON_GetObjectItem(result, "content"), 0);
    assert(!strcmp(cJSON_GetObjectItem(content, "text")->valuestring, expected));
    cJSON_Delete(result);
}

int main()
{
    check_result(ToolError{"UNO offline"}, true, "UNO offline");
    check_result(std::string("queued"), false, "queued");
    puts("PASS: MCP robot errors set isError, queued results remain successful");
    check_result(true, false, "true");
    check_result(false, false, "false");
    check_result(90, false, "90");
    puts("PASS: existing boolean/integer MCP results remain compatible");

    Property pan("pan", kPropertyTypeInteger, 20, 160);
    assert(pan.accepts_integer(20) && pan.accepts_integer(160) && pan.accepts_integer(90));
    for (double bad : {19.0, 161.0, 90.5, 1e30, -1e30,
                       std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()})
        assert(!pan.accepts_integer(bad));
    cJSON *schema = cJSON_Parse(pan.to_json().c_str());
    assert(cJSON_GetObjectItem(schema, "minimum")->valueint == 20);
    assert(cJSON_GetObjectItem(schema, "maximum")->valueint == 160);
    cJSON_Delete(schema);
    puts("PASS: MCP angles reject fractions, overflow, NaN and out-of-range values");

    std::vector<McpTool *> tools;
    for (int i = 0; i < 9; ++i)
        tools.push_back(new McpTool("self.robot.test" + std::to_string(i),
            std::string(550, 'x') + "点头摇头", PropertyList(),
            [](const PropertyList &) -> ReturnValue { return true; }));
    std::string cursor;
    size_t seen = 0, pages = 0;
    do {
        McpToolPage page = BuildMcpToolPage(tools, cursor);
        assert(page.error.empty() && page.count > 0);
        assert(page.json.size() <= MCP_TOOL_PAGE_BYTES);
        // Include a realistic reply envelope and a maximum-length session ID.
        std::string envelope = "{\"session_id\":\"" + std::string(63, 's') +
            "\",\"type\":\"mcp\",\"payload\":{\"jsonrpc\":\"2.0\",\"id\":123,\"result\":" + page.json + "}}";
        assert(envelope.size() < 1420);
        cJSON *json = cJSON_Parse(page.json.c_str());
        assert(json);
        cJSON *array = cJSON_GetObjectItem(json, "tools");
        for (int j = 0; j < cJSON_GetArraySize(array); ++j) {
            assert(cJSON_GetObjectItem(cJSON_GetArrayItem(array, j), "name")->valuestring == tools[seen]->name());
            ++seen;
        }
        cJSON *next = cJSON_GetObjectItem(json, "nextCursor");
        assert((next ? next->valuestring : "") == page.next_cursor);
        cJSON_Delete(json);
        cursor = page.next_cursor;
        assert(++pages <= tools.size());
    } while (!cursor.empty());
    assert(seen == tools.size() && pages > 1);
    assert(!BuildMcpToolPage(tools, "missing").error.empty());
    for (auto tool : tools) delete tool;
    tools.clear();
    assert(BuildMcpToolPage(tools, "").json == "{\"tools\":[]}");
    tools.push_back(new McpTool("too_big", std::string(2000, 'x'), PropertyList(),
                               [](const PropertyList &) -> ReturnValue { return true; }));
    assert(!BuildMcpToolPage(tools, "").error.empty());
    delete tools[0];
    puts("PASS: MCP tool pagination stays below transport budget, no lost/duplicate tools");

    char session[XZ_SESSION_ID_CAPACITY];
    const char *uuid = "c381e0d2-bfd5-4e2b-9dcc-e609b51455a2";
    assert(xz_copy_session_id(session, sizeof(session), uuid) && !strcmp(session, uuid));
    assert(xz_copy_session_id(session, sizeof(session), "short") && !strcmp(session, "short"));
    assert(xz_copy_session_id(session, sizeof(session), std::string(63, 's').c_str()));
    assert(!xz_copy_session_id(session, sizeof(session), std::string(64, 's').c_str()) && !session[0]);
    assert(!xz_copy_session_id(session, sizeof(session), nullptr));
    assert(!xz_copy_session_id(session, sizeof(session), ""));
    puts("PASS: full UUID sessions retained, short reconnect IDs replace old data, oversize rejected");
}
