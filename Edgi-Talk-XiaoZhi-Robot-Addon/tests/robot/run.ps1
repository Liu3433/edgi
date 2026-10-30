$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$output = Join-Path $project 'build/robot_host_tests'
New-Item -ItemType Directory -Force -Path $output | Out-Null
& gcc -std=c99 -Wall -Wextra -Werror -c (Join-Path $project 'applications/robot/robot_link.c') -o (Join-Path $output 'robot_link.o')
if ($LASTEXITCODE -ne 0) { throw 'Protocol core compilation failed' }
& g++ -std=c++17 -Wall -Wextra -Werror -I (Join-Path $PSScriptRoot 'stubs') (Join-Path $PSScriptRoot 'test_interop.cpp') (Join-Path $output 'robot_link.o') -o (Join-Path $output 'test_interop.exe')
if ($LASTEXITCODE -ne 0) { throw 'UNO interoperability test compilation failed' }
& (Join-Path $output 'test_interop.exe')
if ($LASTEXITCODE -ne 0) { throw 'UNO interoperability tests failed' }
$cjson = Join-Path $project 'packages/cJSON-v1.7.17'
& gcc -std=c99 -I (Join-Path $PSScriptRoot 'stubs') -c (Join-Path $cjson 'cJSON.c') -o (Join-Path $output 'cJSON.o')
if ($LASTEXITCODE -ne 0) { throw 'cJSON compilation failed' }
& g++ -std=c++17 -Wall -Wextra -Werror -I (Join-Path $PSScriptRoot 'stubs') -I $cjson (Join-Path $PSScriptRoot 'test_mcp.cpp') (Join-Path $output 'cJSON.o') -o (Join-Path $output 'test_mcp.exe')
if ($LASTEXITCODE -ne 0) { throw 'MCP test compilation failed' }
& (Join-Path $output 'test_mcp.exe')
if ($LASTEXITCODE -ne 0) { throw 'MCP tests failed' }
& python (Join-Path $PSScriptRoot 'extract_websocket_test.py')
if ($LASTEXITCODE -ne 0) { throw 'WebSocket source extraction failed' }
& gcc -std=gnu99 -Wall -Wextra -Wno-format -I $output (Join-Path $PSScriptRoot 'test_websocket.c') -o (Join-Path $output 'test_websocket.exe')
if ($LASTEXITCODE -ne 0) { throw 'WebSocket regression test compilation failed' }
& (Join-Path $output 'test_websocket.exe')
if ($LASTEXITCODE -ne 0) { throw 'WebSocket regression test failed' }
