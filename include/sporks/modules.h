/************************************************************************************
 * 
 * TriviaBot, the Discord Quiz Bot with over 80,000 questions!
 *
 * Copyright 2004 Craig Edwards <support@sporks.gg>
 *
 * Core based on Sporks, the Learning Discord Bot, Craig Edwards (c) 2019.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ************************************************************************************/

#pragma once
#include <sporks/bot.h>
#include <atomic>

class Module;
class ModuleLoader;

#define modevent aegis::gateway::events

/** Implementation-specific flags which may be set in Module constructor by calling Attach()
 */
enum Implementation
{
	I_BEGIN,
	I_OnMessage,
	I_OnReady,
	I_OnChannelCreate,
	I_OnChannelDelete,
	I_OnGuildMemberAdd,
	I_OnGuildCreate,
	I_OnGuildDelete,
	I_OnPresenceUpdate,
	I_OnRestEnd,
	I_OnAllShardsReady,
	I_OnTypingStart,
	I_OnMessageUpdate,
	I_OnMessageDelete,
	I_OnMessageDeleteBulk,
	I_OnGuildUpdate,
	I_OnMessageReactionAdd,
	I_OnMessageReactionRemove,
	I_OnMessageReactionRemoveAll,
	I_OnUserUpdate,
	I_OnResumed,
	I_OnChannelUpdate,
	I_OnChannelPinsUpdate,
	I_OnGuildBanAdd,
	I_OnGuildBanRemove,
	I_OnGuildEmojisUpdate,
	I_OnGuildIntegrationsUpdate,
	I_OnGuildMemberRemove,
	I_OnGuildMemberUpdate,
	I_OnGuildMembersChunk,
	I_OnGuildRoleCreate,
	I_OnGuildRoleUpdate,
	I_OnGuildRoleDelete,
	I_OnPresenceUpdateWS,
	I_OnVoiceStateUpdate,
	I_OnVoiceServerUpdate,
	I_OnWebhooksUpdate,
	I_END
};

/**
 * This #define allows us to call a method in all loaded modules in a readable simple way, e.g.:
 * 'FOREACH_MOD(I_OnGuildAdd,OnGuildAdd(guildinfo));'
 * NOTE: Locks mutex - two FOREACH_MOD() can't run asyncronously in two different threads. This is
 * to prevent one thread loading/unloading a module, changing the arrays/vectors while another thread
 * is running an event.
 */
#define FOREACH_MOD(y,x) { \
	std::vector<Module*> list_to_call; \
	{ \
		std::lock_guard l(Loader->mtx); \
		list_to_call = Loader->EventHandlers[y]; \
	} \
	for (auto _i = list_to_call.begin(); _i != list_to_call.end(); ++_i) \
	{ \
		try \
		{ \
			if (!(*_i)->x) { \
				break; \
			} \
		} \
		catch (std::exception& modexcept) \
		{ \
			core.log->error("Exception caught in module: {}", modexcept.what()); \
		} \
	} \
};


/** Defines the signature of the module's entrypoint function */
typedef Module* (initfunctype) (Bot*, ModuleLoader*);

/** A map representing the list of modules as external systems see it */
typedef std::map<std::string, Module*> ModMap;

/**
 * ModuleNative contains the OS level details of the module, e.g. the handle returned by dlopen()
 * and the last error message string, also a pointer to the init_module() function within the module.
 */
struct ModuleNative {
	initfunctype* init;
	void* dlopen_handle;
	const char *err;
	Module* module_object;
};

/**
 * ModuleLoader handles loading and unloading of modules at runtime, and maintains a list of loaded
 * modules. It can be queried for this list.
 */
class ModuleLoader {
	Bot* bot;

	/* A map of ModuleNatives used to manage the loaded module list */
	std::map<std::string, ModuleNative> Modules;

	/* Retrieve a named symbol from a shared object file */
	bool GetSymbol(ModuleNative &native, const char *sym_name);

	/* The module list as external systems see it, a map of string filenames to
	 * module objects
	 */
	ModMap ModuleList;

	std::string lasterror;
public:
	/* Module loader mutex */
	std::mutex mtx;

	/* An array of vectors indicating which modules are watching which events */
	std::vector<Module*> EventHandlers[I_END];

	ModuleLoader(Bot* creator);
	virtual ~ModuleLoader();

	/* Attach a module to an event. Only events a module explicitly attaches to will be
	 * called for that module, this allows a module to turn events on and off as it needs
	 * on the fly.
	 */
	void Attach(const std::vector<Implementation> &i, Module* mod);

	/* Detach a module from an event, opposite of Attach()
	 */
	void Detach(const std::vector<Implementation> &i, Module* mod);

	/* Load a module from a shared object file. The path is relative to the bot's executable.
	 */
	bool Load(const std::string &filename);

	/* Unload a module from memory. Calls the Module class's destructor and then dlclose().
	 */
	bool Unload(const std::string &filename);

	/* Unload and then Load a module
	 */
	bool Reload(const std::string &filename);

	/* Enumerate modules within modules.json and load them all. Any modules that are
	 * already loaded will be ignored, so we can use this to load new modules on rehash
	 */
	void LoadAll();

	/* Get a list of all loaded modules */
	const ModMap& GetModuleList() const;

	const std::string& GetLastError();
};

/**
 * All modules that can be loaded at runtime derive from the Module class. This class contains
 * virtual methods for each event triggered by the aegis library that the bot handles internally,
 * plus some helper methods to identify and report the version number of the module.
 */
class Module {
protected:
	/* Pointer back to the bot instance that created this module */
	Bot* bot;
public:
	/* Instantiate a module, the ModuleLoader is passed in so that we can attach events */
	Module(Bot* instigator, ModuleLoader* ml);

	virtual ~Module();

	/* Report version and description of module */
	virtual std::string GetVersion();
	virtual std::string GetDescription();

	/* Aegis events */
	virtual bool OnChannelCreate(const modevent::channel_create &channel);
	virtual bool OnReady(const modevent::ready &ready);
	virtual bool OnChannelDelete(const modevent::channel_delete &channel);
	virtual bool OnGuildCreate(const modevent::guild_create &guild);
	virtual bool OnGuildDelete(const modevent::guild_delete &guild);
	virtual bool OnGuildMemberAdd(const modevent::guild_member_add &gma);
	virtual bool OnMessage(const modevent::message_create &message, const std::string& clean_message, bool mentioned, const std::vector<std::string> &stringmentions);
	virtual bool OnPresenceUpdate();
	virtual bool OnRestEnd(std::chrono::steady_clock::time_point start_time, uint16_t code);
	virtual bool OnAllShardsReady();
	virtual bool OnTypingStart(const modevent::typing_start &obj);
	virtual bool OnMessageUpdate(const modevent::message_update &obj);
	virtual bool OnMessageDelete(const modevent::message_delete &obj);
	virtual bool OnMessageDeleteBulk(const modevent::message_delete_bulk &obj);
	virtual bool OnGuildUpdate(const modevent::guild_update &obj);
	virtual bool OnMessageReactionAdd(const modevent::message_reaction_add &obj);
	virtual bool OnMessageReactionRemove(const modevent::message_reaction_remove &obj);
	virtual bool OnMessageReactionRemoveAll(const modevent::message_reaction_remove_all &obj);
	virtual bool OnUserUpdate(const modevent::user_update &obj);
	virtual bool OnResumed(const modevent::resumed &obj);
	virtual bool OnChannelUpdate(const modevent::channel_update &obj);
	virtual bool OnChannelPinsUpdate(const modevent::channel_pins_update &obj);
	virtual bool OnGuildBanAdd(const modevent::guild_ban_add &obj);
	virtual bool OnGuildBanRemove(const modevent::guild_ban_remove &obj);
	virtual bool OnGuildEmojisUpdate(const modevent::guild_emojis_update &obj);
	virtual bool OnGuildIntegrationsUpdate(const modevent::guild_integrations_update &obj);
	virtual bool OnGuildMemberRemove(const modevent::guild_member_remove &obj);
	virtual bool OnGuildMemberUpdate(const modevent::guild_member_update &obj);
	virtual bool OnGuildMembersChunk(const modevent::guild_members_chunk &obj);
	virtual bool OnGuildRoleCreate(const modevent::guild_role_create &obj);
	virtual bool OnGuildRoleUpdate(const modevent::guild_role_update &obj);
	virtual bool OnGuildRoleDelete(const modevent::guild_role_delete &obj);
	virtual bool OnPresenceUpdateWS(const modevent::presence_update &obj);
	virtual bool OnVoiceStateUpdate(const modevent::voice_state_update &obj);
	virtual bool OnVoiceServerUpdate(const modevent::voice_server_update &obj);
	virtual bool OnWebhooksUpdate(const modevent::webhooks_update &obj);

	/* Emit a simple text only embed to a channel, many modules use this for error reporting */
	void EmbedSimple(const std::string &message, int64_t channelID);
};


/* A macro that lets us simply define the entrypoint of a module by name */
#define ENTRYPOINT(mod_class_name) extern "C" Module* init_module(Bot* instigator, ModuleLoader* ml) { return new mod_class_name(instigator, ml); }

