#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define NANOSECOND_MULTIPLIER 0.000000001
#define NANOSECONDS_IN_SEC 1000000000

static const int OPAQUE_REGISTRY_KEY = 0;

typedef struct StackItem {
    char *key;
    size_t keyn;
    int is_tail;
    struct timespec *start_time;
} StackItem;

typedef struct ProfileItem {
    char *key;
    size_t keyn;
    long long calls;
    long long time_sec;
    long time_nsec;
} ProfileItem;

static struct timespec *get_time() {
    struct timespec *ts = malloc(sizeof(struct timespec));
    clock_gettime(CLOCK_MONOTONIC_RAW, ts);
    return ts;
}

static inline double calc_time_delta(
    struct timespec *ts1,
    struct timespec *ts2
) {
    return ts2->tv_sec - ts1->tv_sec + \
        NANOSECOND_MULTIPLIER * (ts2->tv_nsec - ts1->tv_nsec);
}

static int profile_item_meta_index(lua_State *L) {
    const char *index_key;
    size_t index_keyn;
    ProfileItem *self;

    self = luaL_checkudata(L, 1, "ProfileItem");
    index_key = luaL_checklstring(L, 2, &index_keyn);

    if (strncmp(index_key, "key", 3) == 0) {
        lua_pushlstring(L, self->key, self->keyn);
    } else if (strncmp(index_key, "calls", 5) == 0) {
        lua_pushinteger(L, (lua_Integer) self->calls);
    } else if (strncmp(index_key, "time", 4) == 0) {
        lua_pushnumber(L, self->time_sec + \
                NANOSECOND_MULTIPLIER * self->time_nsec);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

static int profile_item_meta_gc(lua_State *L) {
    ProfileItem *self = (ProfileItem *) luaL_checkudata(L, 1, "ProfileItem");
    free(self->key);
    lua_pop(L, 1);

    return 0;
}

static int get_profile_state(lua_State *L) {
    return lua_rawgetp(L, LUA_REGISTRYINDEX, (void *) &OPAQUE_REGISTRY_KEY);
}

static void create_profile_state(lua_State *L) {
    lua_newtable(L);

    lua_newtable(L);
    lua_setfield(L, -2, "profile");

    lua_newtable(L);
    lua_setfield(L, -2, "stack");

    lua_rawsetp(L, LUA_REGISTRYINDEX, (void *) &OPAQUE_REGISTRY_KEY);
}

static int remove_profile_state(lua_State *L) {
    get_profile_state(L);
    lua_getfield(L, -1, "profile");
    lua_remove(L, -2);

    lua_pushnil(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, (void *) &OPAQUE_REGISTRY_KEY);

    return 1;
}

static StackItem *new_stack_item(char *key, size_t keyn, int is_tail) {
    struct timespec *time = get_time();
    StackItem *si = malloc(sizeof(StackItem));

    si->key = key;
    si->keyn = keyn;
    si->is_tail = is_tail;
    si->start_time = time;

    return si;
}

static ProfileItem *new_profile_item(lua_State *L) {
    ProfileItem *pi;

    pi = (ProfileItem *) lua_newuserdata(L, sizeof(ProfileItem));
    luaL_getmetatable(L, "ProfileItem");
    lua_setmetatable(L, -2);

    return pi;
}

static char *get_ar_key(lua_State *L, lua_Debug *ar, size_t *keyn, int anon) {
    luaL_Buffer func_name;
    const char *stack_key;
    char *key;
    const void *fptr;

    luaL_buffinit(L, &func_name);
    luaL_addstring(&func_name, ar->short_src);

    if (ar->linedefined != -1) {
        luaL_addchar(&func_name, ':');
        lua_pushinteger(L, ar->linedefined);
        luaL_tolstring(L, -1, NULL);
        lua_remove(L, -2);
        luaL_addvalue(&func_name);
    }

    luaL_addchar(&func_name, ' ');

    if (ar->name == NULL) {
        if (anon) {
            lua_getinfo(L, "f", ar);
            fptr = lua_topointer(L, -1);
            lua_pop(L, 1);
            lua_pushfstring(L, "@<%p>", fptr);
            luaL_addvalue(&func_name);
        } else {
            luaL_addstring(&func_name, "<anon>");
        }
    } else {
        luaL_addstring(&func_name, ar->name);
    }

    luaL_pushresult(&func_name);

    stack_key = lua_tolstring(L, -1, keyn);
    key = calloc(*keyn, sizeof(char));
    strncpy(key, stack_key, *keyn);

    stack_key = NULL;
    lua_pop(L, 1);

    return key;
}

static StackItem *pop_from_stack(lua_State *L) {
    size_t stack_len = luaL_len(L, -1);
    StackItem *si;

    lua_geti(L, -1, stack_len);  // @<StackItem *>
    lua_pushnil(L);
    lua_seti(L, -3, stack_len);  // stack[stack_len] = nil
    si = (StackItem *) lua_touserdata(L, -1);
    lua_pop(L, 1);

    return si;
}

static ProfileItem *get_profile_item(
    lua_State *L,
    char *key,
    size_t keyn
) {
    ProfileItem *pi;

    lua_pushlstring(L, key, keyn);

    if (lua_gettable(L, -2) == LUA_TNIL) {
        lua_pop(L, 1);
        lua_pushlstring(L, key, keyn);

        pi = new_profile_item(L);
        pi->key = key;
        pi->keyn = keyn;
        pi->calls = 0;
        pi->time_sec = 0;
        pi->time_nsec = 0;

        lua_settable(L, -3);
    } else {
        pi = (ProfileItem *) lua_touserdata(L, -1);
        lua_pop(L, 1);
    }

    return pi;
}

static void hook(lua_State *L, lua_Debug *ar) {
    StackItem *si;
    ProfileItem *pi;
    char *key;
    size_t keyn;
    struct timespec *ts;
    int is_tail;
    int obscureAnonymous;

    if (get_profile_state(L) == LUA_TNIL) {
        // the data is wiped
        lua_pop(L, 1);
        return;
    }

    if (ar->event == LUA_HOOKCALL || ar->event == LUA_HOOKTAILCALL) {
        lua_getfield(L, -1, "obscureAnonymous");
        obscureAnonymous = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getinfo(L, "nSt", ar);

        lua_getfield(L, -1, "stack");
        key = get_ar_key(L, ar, &keyn, obscureAnonymous);
        si = new_stack_item(key, keyn, ar->event == LUA_HOOKTAILCALL);
        lua_pushlightuserdata(L, (void *) si);
        lua_seti(L, -2, luaL_len(L, -2) + 1);
        lua_pop(L, 2);
    } else if (ar->event == LUA_HOOKRET) {
        lua_getfield(L, -1, "profile");
        lua_getfield(L, -2, "stack");

        do {
            if (luaL_len(L, -1) == 0) {
                /* We don't have anything to do with calls not registered by a
                 * a HOOKCALL/HOOKTAILCALL event, in which case the stack is
                 * empty.
                 */
                break;
            }

            si = pop_from_stack(L);

            lua_rotate(L, -2, 1);
            pi = get_profile_item(L, si->key, si->keyn);
            lua_rotate(L, -2, 1);

            ts = get_time();
            pi->time_sec = pi->time_sec + ts->tv_sec - si->start_time->tv_sec;
            pi->time_nsec = pi->time_nsec + \
                            ts->tv_nsec - si->start_time->tv_nsec;

            if (pi->time_nsec >= NANOSECONDS_IN_SEC) {
                pi->time_sec = pi->time_sec + pi->time_nsec / NANOSECONDS_IN_SEC;
                pi->time_nsec = pi->time_nsec % NANOSECONDS_IN_SEC;
            }

            ++pi->calls;

            is_tail = si->is_tail;

            free(ts);
            free(si->start_time);
            free(si);
        } while (is_tail);

        lua_pop(L, 3);
    }
}

static int set_profile_hook(lua_State *L) {
    int obscureAnonymous = lua_toboolean(L, 1);

    create_profile_state(L);
    get_profile_state(L);

    lua_pushlightuserdata(L, (void *) get_time());
    lua_setfield(L, -2, "start");

    lua_pushboolean(L, obscureAnonymous);
    lua_setfield(L, -2, "obscureAnonymous");

    lua_pop(L, 1);
    lua_sethook(L, hook, LUA_MASKCALL | LUA_MASKRET, 0);
    return 0;
}

static int unset_profile_hook(lua_State *L) {
    struct timespec *ts1;
    struct timespec *ts2;

    lua_sethook(L, hook, 0, 0);

    get_profile_state(L);
    lua_getfield(L, -1, "start");
    ts1 = lua_touserdata(L, -1);
    lua_pop(L, 2);
    ts2 = get_time();

    lua_pushnumber(L, (lua_Number) calc_time_delta(ts1, ts2));

    free(ts1);
    free(ts2);

    return 1;
}

static const struct luaL_Reg lib[] = {
    {"setProfileHook", set_profile_hook},
    {"unsetProfileHook", unset_profile_hook},
    {"wipeProfileData", remove_profile_state},
    {NULL, NULL}
};

static const struct luaL_Reg profile_item_meta[] = {
    {"__index", profile_item_meta_index},
    {"__gc", profile_item_meta_gc},
    {NULL, NULL}
};

int luaopen_profile_hook(lua_State *L) {
    luaL_newmetatable(L, "ProfileItem");
    luaL_setfuncs(L, profile_item_meta, 0);
    luaL_newlib(L, lib);
    return 1;
}

// vim: autoindent tabstop=4 shiftwidth=4 expandtab
