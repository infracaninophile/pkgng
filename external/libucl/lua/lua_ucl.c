/* Copyright (c) 2014, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file lua ucl bindings
 */

#include "ucl.h"
#include "ucl_internal.h"
#include "lua_ucl.h"

#define PARSER_META "ucl.parser.meta"
#define EMITTER_META "ucl.emitter.meta"
#define NULL_META "null.emitter.meta"

static int ucl_object_lua_push_array (lua_State *L, const ucl_object_t *obj);
static int ucl_object_lua_push_scalar (lua_State *L, const ucl_object_t *obj, bool allow_array);
static ucl_object_t* ucl_object_lua_fromtable (lua_State *L, int idx);
static ucl_object_t* ucl_object_lua_fromelt (lua_State *L, int idx);

static void *ucl_null;

/**
 * Push a single element of an object to lua
 * @param L
 * @param key
 * @param obj
 */
static void
ucl_object_lua_push_element (lua_State *L, const char *key,
		const ucl_object_t *obj)
{
	lua_pushstring (L, key);
	ucl_object_push_lua (L, obj, true);
	lua_settable (L, -3);
}

/**
 * Push a single object to lua
 * @param L
 * @param obj
 * @return
 */
static int
ucl_object_lua_push_object (lua_State *L, const ucl_object_t *obj,
		bool allow_array)
{
	const ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	int nelt = 0;

	if (allow_array && obj->next != NULL) {
		/* Actually we need to push this as an array */
		return ucl_object_lua_push_array (L, obj);
	}

	/* Optimize allocation by preallocation of table */
	while (ucl_iterate_object (obj, &it, true) != NULL) {
		nelt ++;
	}

	lua_createtable (L, 0, nelt);
	it = NULL;

	while ((cur = ucl_iterate_object (obj, &it, true)) != NULL) {
		ucl_object_lua_push_element (L, ucl_object_key (cur), cur);
	}

	return 1;
}

/**
 * Push an array to lua as table indexed by integers
 * @param L
 * @param obj
 * @return
 */
static int
ucl_object_lua_push_array (lua_State *L, const ucl_object_t *obj)
{
	const ucl_object_t *cur;
	int i = 1, nelt = 0;

	/* Optimize allocation by preallocation of table */
	LL_FOREACH (obj, cur) {
		nelt ++;
	}

	lua_createtable (L, nelt, 0);

	LL_FOREACH (obj, cur) {
		ucl_object_push_lua (L, cur, false);
		lua_rawseti (L, -2, i);
		i ++;
	}

	return 1;
}

/**
 * Push a simple object to lua depending on its actual type
 */
static int
ucl_object_lua_push_scalar (lua_State *L, const ucl_object_t *obj,
		bool allow_array)
{
	if (allow_array && obj->next != NULL) {
		/* Actually we need to push this as an array */
		return ucl_object_lua_push_array (L, obj);
	}

	switch (obj->type) {
	case UCL_BOOLEAN:
		lua_pushboolean (L, ucl_obj_toboolean (obj));
		break;
	case UCL_STRING:
		lua_pushstring (L, ucl_obj_tostring (obj));
		break;
	case UCL_INT:
#if LUA_VERSION_NUM >= 501
		lua_pushinteger (L, ucl_obj_toint (obj));
#else
		lua_pushnumber (L, ucl_obj_toint (obj));
#endif
		break;
	case UCL_FLOAT:
	case UCL_TIME:
		lua_pushnumber (L, ucl_obj_todouble (obj));
		break;
	case UCL_NULL:
		lua_getfield (L, LUA_REGISTRYINDEX, "ucl.null");
		break;
	default:
		lua_pushnil (L);
		break;
	}

	return 1;
}

/**
 * Push an object to lua
 * @param L lua state
 * @param obj object to push
 */
int
ucl_object_push_lua (lua_State *L, const ucl_object_t *obj, bool allow_array)
{
	switch (obj->type) {
	case UCL_OBJECT:
		return ucl_object_lua_push_object (L, obj, allow_array);
	case UCL_ARRAY:
		return ucl_object_lua_push_array (L, obj->value.av);
	default:
		return ucl_object_lua_push_scalar (L, obj, allow_array);
	}
}

/**
 * Parse lua table into object top
 * @param L
 * @param top
 * @param idx
 */
static ucl_object_t *
ucl_object_lua_fromtable (lua_State *L, int idx)
{
	ucl_object_t *obj, *top = NULL;
	size_t keylen;
	const char *k;
	bool is_array = true;
	int max = INT_MIN;

	if (idx < 0) {
		/* For negative indicies we want to invert them */
		idx = lua_gettop (L) + idx + 1;
	}
	/* Check for array */
	lua_pushnil (L);
	while (lua_next (L, idx) != 0) {
		if (lua_type (L, -2) == LUA_TNUMBER) {
			double num = lua_tonumber (L, -2);
			if (num == (int)num) {
				if (num > max) {
					max = num;
				}
			}
			else {
				/* Keys are not integer */
				lua_pop (L, 2);
				is_array = false;
				break;
			}
		}
		else {
			/* Keys are not numeric */
			lua_pop (L, 2);
			is_array = false;
			break;
		}
		lua_pop (L, 1);
	}

	/* Table iterate */
	if (is_array) {
		int i;

		top = ucl_object_typed_new (UCL_ARRAY);
		for (i = 1; i <= max; i ++) {
			lua_pushinteger (L, i);
			lua_gettable (L, idx);
			obj = ucl_object_lua_fromelt (L, lua_gettop (L));
			if (obj != NULL) {
				ucl_array_append (top, obj);
			}
		}
	}
	else {
		lua_pushnil (L);
		top = ucl_object_typed_new (UCL_OBJECT);
		while (lua_next (L, idx) != 0) {
			/* copy key to avoid modifications */
			k = lua_tolstring (L, -2, &keylen);
			obj = ucl_object_lua_fromelt (L, lua_gettop (L));

			if (obj != NULL) {
				ucl_object_insert_key (top, obj, k, keylen, true);
			}
			lua_pop (L, 1);
		}
	}

	return top;
}

/**
 * Get a single element from lua to object obj
 * @param L
 * @param obj
 * @param idx
 */
static ucl_object_t *
ucl_object_lua_fromelt (lua_State *L, int idx)
{
	int type;
	double num;
	ucl_object_t *obj = NULL;

	type = lua_type (L, idx);

	switch (type) {
	case LUA_TSTRING:
		obj = ucl_object_fromstring_common (lua_tostring (L, idx), 0, 0);
		break;
	case LUA_TNUMBER:
		num = lua_tonumber (L, idx);
		if (num == (int64_t)num) {
			obj = ucl_object_fromint (num);
		}
		else {
			obj = ucl_object_fromdouble (num);
		}
		break;
	case LUA_TBOOLEAN:
		obj = ucl_object_frombool (lua_toboolean (L, idx));
		break;
	case LUA_TUSERDATA:
		if (lua_topointer (L, idx) == ucl_null) {
			obj = ucl_object_typed_new (UCL_NULL);
		}
		break;
	case LUA_TTABLE:
	case LUA_TFUNCTION:
	case LUA_TTHREAD:
		if (luaL_getmetafield (L, idx, "__gen_ucl")) {
			if (lua_isfunction (L, -1)) {
				lua_settop (L, 3); /* gen, obj, func */
				lua_insert (L, 1); /* func, gen, obj */
				lua_insert (L, 2); /* func, obj, gen */
				lua_call(L, 2, 1);
				obj = ucl_object_lua_fromelt (L, 1);
			}
			lua_pop (L, 2);
		}
		if (type == LUA_TTABLE) {
			obj = ucl_object_lua_fromtable (L, idx);
		}
		else if (type == LUA_TFUNCTION) {
			lua_pushvalue (L, idx);
			obj = ucl_object_new ();
			obj->type = UCL_USERDATA;
			obj->value.ud = (void *)(uintptr_t)(int)(luaL_ref (L, LUA_REGISTRYINDEX));
		}
		break;
	}

	return obj;
}

/**
 * Extract rcl object from lua object
 * @param L
 * @return
 */
ucl_object_t *
ucl_object_lua_import (lua_State *L, int idx)
{
	ucl_object_t *obj;
	int t;

	t = lua_type (L, idx);
	switch (t) {
	case LUA_TTABLE:
		obj = ucl_object_lua_fromtable (L, idx);
		break;
	default:
		obj = ucl_object_lua_fromelt (L, idx);
		break;
	}

	return obj;
}

static int
lua_ucl_parser_init (lua_State *L)
{
	struct ucl_parser *parser, **pparser;
	int flags = 0;

	if (lua_gettop (L) >= 1) {
		flags = lua_tonumber (L, 1);
	}

	parser = ucl_parser_new (flags);
	if (parser == NULL) {
		lua_pushnil (L);
	}

	pparser = lua_newuserdata (L, sizeof (parser));
	*pparser = parser;
	luaL_getmetatable (L, PARSER_META);
	lua_setmetatable (L, -2);

	return 1;
}

static struct ucl_parser *
lua_ucl_parser_get (lua_State *L, int index)
{
	return *((struct ucl_parser **) luaL_checkudata(L, index, PARSER_META));
}

static int
lua_ucl_parser_parse_file (lua_State *L)
{
	struct ucl_parser *parser;
	const char *file;
	int ret = 2;

	parser = lua_ucl_parser_get (L, 1);
	file = luaL_checkstring (L, 2);

	if (parser != NULL && file != NULL) {
		if (ucl_parser_add_file (parser, file)) {
			lua_pushboolean (L, true);
			ret = 1;
		}
		else {
			lua_pushboolean (L, false);
			lua_pushstring (L, ucl_parser_get_error (parser));
		}
	}
	else {
		lua_pushboolean (L, false);
		lua_pushstring (L, "invalid arguments");
	}

	return ret;
}

static int
lua_ucl_parser_parse_string (lua_State *L)
{
	struct ucl_parser *parser;
	const char *string;
	size_t llen;
	int ret = 2;

	parser = lua_ucl_parser_get (L, 1);
	string = luaL_checklstring (L, 2, &llen);

	if (parser != NULL && string != NULL) {
		if (ucl_parser_add_chunk (parser, (const unsigned char *)string, llen)) {
			lua_pushboolean (L, true);
			ret = 1;
		}
		else {
			lua_pushboolean (L, false);
			lua_pushstring (L, ucl_parser_get_error (parser));
		}
	}
	else {
		lua_pushboolean (L, false);
		lua_pushstring (L, "invalid arguments");
	}

	return ret;
}

static int
lua_ucl_parser_get_object (lua_State *L)
{
	struct ucl_parser *parser;
	ucl_object_t *obj;
	int ret = 1;

	parser = lua_ucl_parser_get (L, 1);
	obj = ucl_parser_get_object (parser);

	if (obj != NULL) {
		ret = ucl_object_push_lua (L, obj, false);
		/* no need to keep reference */
		ucl_object_unref (obj);
	}
	else {
		lua_pushnil (L);
	}

	return ret;
}

static int
lua_ucl_parser_gc (lua_State *L)
{
	struct ucl_parser *parser;

	parser = lua_ucl_parser_get (L, 1);
	ucl_parser_free (parser);

	return 0;
}

static void
lua_ucl_parser_mt (lua_State *L)
{
	luaL_newmetatable (L, PARSER_META);

	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

	lua_pushcfunction (L, lua_ucl_parser_gc);
	lua_setfield (L, -2, "__gc");

	lua_pushcfunction (L, lua_ucl_parser_parse_file);
	lua_setfield (L, -2, "parse_file");

	lua_pushcfunction (L, lua_ucl_parser_parse_string);
	lua_setfield (L, -2, "parse_string");

	lua_pushcfunction (L, lua_ucl_parser_get_object);
	lua_setfield (L, -2, "get_object");

	lua_pop (L, 1);
}

static int
lua_ucl_to_string (lua_State *L, const ucl_object_t *obj, enum ucl_emitter type)
{
	unsigned char *result;

	result = ucl_object_emit (obj, type);

	if (result != NULL) {
		lua_pushstring (L, (const char *)result);
		free (result);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

static int
lua_ucl_to_json (lua_State *L)
{
	ucl_object_t *obj;
	int format = UCL_EMIT_JSON;

	if (lua_gettop (L) > 1) {
		if (lua_toboolean (L, 2)) {
			format = UCL_EMIT_JSON_COMPACT;
		}
	}

	obj = ucl_object_lua_import (L, 1);
	if (obj != NULL) {
		lua_ucl_to_string (L, obj, format);
		ucl_object_unref (obj);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

static int
lua_ucl_to_config (lua_State *L)
{
	ucl_object_t *obj;

	obj = ucl_object_lua_import (L, 1);
	if (obj != NULL) {
		lua_ucl_to_string (L, obj, UCL_EMIT_CONFIG);
		ucl_object_unref (obj);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

static int
lua_ucl_to_format (lua_State *L)
{
	ucl_object_t *obj;
	int format = UCL_EMIT_JSON;

	if (lua_gettop (L) > 1) {
		format = lua_tonumber (L, 2);
		if (format < 0 || format >= UCL_EMIT_YAML) {
			lua_pushnil (L);
			return 1;
		}
	}

	obj = ucl_object_lua_import (L, 1);
	if (obj != NULL) {
		lua_ucl_to_string (L, obj, format);
		ucl_object_unref (obj);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

static int
lua_ucl_null_tostring (lua_State* L)
{
	lua_pushstring (L, "null");
	return 1;
}

static void
lua_ucl_null_mt (lua_State *L)
{
	luaL_newmetatable (L, NULL_META);

	lua_pushcfunction (L, lua_ucl_null_tostring);
	lua_setfield (L, -2, "__tostring");

	lua_pop (L, 1);
}

int
luaopen_ucl (lua_State *L)
{
	lua_ucl_parser_mt (L);
	lua_ucl_null_mt (L);

	/* Create the refs weak table: */
	lua_createtable (L, 0, 2);
	lua_pushliteral (L, "v"); /* tbl, "v" */
	lua_setfield (L, -2, "__mode");
	lua_pushvalue (L, -1); /* tbl, tbl */
	lua_setmetatable (L, -2); /* tbl */
	lua_setfield (L, LUA_REGISTRYINDEX, "ucl.refs");

	lua_newtable (L);

	lua_pushcfunction (L, lua_ucl_parser_init);
	lua_setfield (L, -2, "parser");

	lua_pushcfunction (L, lua_ucl_to_json);
	lua_setfield (L, -2, "to_json");

	lua_pushcfunction (L, lua_ucl_to_config);
	lua_setfield (L, -2, "to_config");

	lua_pushcfunction (L, lua_ucl_to_format);
	lua_setfield (L, -2, "to_format");

	ucl_null = lua_newuserdata (L, 0);
	luaL_getmetatable (L, NULL_META);
	lua_setmetatable (L, -2);

	lua_pushvalue (L, -1);
	lua_setfield (L, LUA_REGISTRYINDEX, "ucl.null");

	lua_setfield (L, -2, "null");

	return 1;
}
