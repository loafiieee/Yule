#include "../mod_json.h"

#include <luajit-2.1/lauxlib.h>
#include <luajit-2.1/lualib.h>

#include <stdio.h>

static int failures = 0;

static void install_json_api(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, mod_json_lua_encode);
    lua_setfield(L, -2, "encode");
    lua_pushcfunction(L, mod_json_lua_decode);
    lua_setfield(L, -2, "decode");
    lua_pushcfunction(L, mod_json_lua_array);
    lua_setfield(L, -2, "array");
    lua_pushcfunction(L, mod_json_lua_object);
    lua_setfield(L, -2, "object");
    lua_pushcfunction(L, mod_json_lua_is_null);
    lua_setfield(L, -2, "is_null");
    mod_json_lua_push_null(L);
    lua_setfield(L, -2, "null");
    lua_setglobal(L, "json");
}

static void check_script(lua_State* L, const char* name, const char* script) {
    int status = luaL_loadstring(L, script);
    if (status == 0) status = lua_pcall(L, 0, 1, 0);
    if (status != 0) {
        fprintf(stderr, "FAIL %s: Lua error: %s\n", name,
                lua_tostring(L, -1));
        lua_pop(L, 1);
        ++failures;
        return;
    }
    if (!lua_toboolean(L, -1)) {
        fprintf(stderr, "FAIL %s: script returned false\n", name);
        ++failures;
    }
    lua_pop(L, 1);
}

int main(void) {
    lua_State* L = luaL_newstate();
    if (!L) {
        fputs("FAIL: could not create Lua state\n", stderr);
        return 1;
    }
    luaL_openlibs(L);
    install_json_api(L);

    check_script(
        L, "deterministic object encoding",
        "local s,e=json.encode({z=1,a='x',m=true});"
        "return s=='{\"a\":\"x\",\"m\":true,\"z\":1}' and e==nil"
    );
    check_script(
        L, "Unicode/null/container round trip",
        "local v,e=json.decode("
        "'{\"a\":[true,null,\"\\\\uD83D\\\\uDE00\"],\"empty\":[]}');"
        "if not v then return false end;"
        "if not json.is_null(v.a[2]) or #v.empty~=0 then return false end;"
        "local s=json.encode(v);"
        "return s=='{\"a\":[true,null,\"😀\"],\"empty\":[]}'"
    );
    check_script(
        L, "explicit empty containers",
        "return json.encode(json.array())=='[]' and "
        "json.encode(json.object())=='{}' and "
        "json.encode({})=='{}'"
    );
    check_script(
        L, "null identity",
        "return json.is_null(json.null) and not json.is_null(nil) and "
        "json.encode(json.null)=='null'"
    );
    check_script(
        L, "duplicate keys rejected with path",
        "local v,e=json.decode('{\"a\":1,\"a\":2}');"
        "return v==nil and e:find('$.a',1,true) and "
        "e:find('duplicate object key',1,true)"
    );
    check_script(
        L, "cycle rejected with path",
        "local v={};v.child=v;local s,e=json.encode(v);"
        "return s==nil and e:find('$.child',1,true) and "
        "e:find('cycle',1,true)"
    );
    check_script(
        L, "sparse array rejected",
        "local s,e=json.encode(json.array({[2]=1}));"
        "return s==nil and e:find('sparse',1,true)"
    );
    check_script(
        L, "mixed table rejected",
        "local s,e=json.encode({[1]='x',name='x'});"
        "return s==nil and e:find('mixed',1,true)"
    );
    check_script(
        L, "invalid JSON/UTF-8 rejected",
        "local a,ea=json.decode('01');"
        "local b,eb=json.decode('\"'..string.char(255)..'\"');"
        "return a==nil and ea:find('invalid JSON number',1,true) and "
        "b==nil and eb:find('UTF%-8')"
    );
    check_script(
        L, "nonfinite and nil rejected",
        "local a,ea=json.encode(0/0);local b,eb=json.encode(nil);"
        "return a==nil and ea:find('non%-finite') and "
        "b==nil and eb:find('mod.json.null',1,true)"
    );
    check_script(
        L, "string/input limits",
        "local a,ea=json.encode(string.rep('x',262145));"
        "local b,eb=json.decode(string.rep(' ',1048577));"
        "return a==nil and ea:find('262144',1,true) and "
        "b==nil and eb:find('1048576',1,true)"
    );
    check_script(
        L, "depth limit",
        "local text=string.rep('[',33)..'0'..string.rep(']',33);"
        "local v,e=json.decode(text);"
        "return v==nil and e:find('depth',1,true)"
    );
    check_script(
        L, "container helpers copy",
        "local source={1,2};local array=json.array(source);source[1]=9;"
        "local object=json.object({x=1});"
        "return json.encode(array)=='[1,2]' and "
        "json.encode(object)=='{\"x\":1}'"
    );

    lua_close(L);
    if (failures != 0) {
        fprintf(stderr, "mod JSON tests failed: %d\n", failures);
        return 1;
    }
    puts("mod JSON parser/encoder tests: OK");
    return 0;
}
