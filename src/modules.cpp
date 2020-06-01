/************************************************************************************
 * 
 * Sporks, the learning, scriptable Discord bot!
 *
 * Copyright 2019 Craig Edwards <support@sporks.gg>
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

#include <sporks/modules.h>
#ifndef _GNU_SOURCE
	#define _GNU_SOURCE
#endif
#include <link.h>
#include <dlfcn.h>
#include <sstream>
#include <sporks/stringops.h>

/**
 * String versions of the enum Implementation values, for display only
 */
const char* StringNames[I_END + 1] = {
	"I_BEGIN",
	"I_OnMessage",
	"I_OnReady",
	"I_OnChannelCreate",
	"I_OnChannelDelete",
	"I_OnGuildMemberAdd",
	"I_OnGuildCreate",
	"I_OnGuildDelete",
	"I_OnPresenceUpdate",
	"I_OnRestEnd",
	"I_OnAllShardsReady",
	"I_OnTypingStart",
	"I_OnMessageUpdate",
	"I_OnMessageDelete",
	"I_OnMessageDeleteBulk",
	"I_OnGuildUpdate",
	"I_OnMessageReactionAdd",
	"I_OnMessageReactionRemove",
	"I_OnMessageReactionRemoveAll",
	"I_OnUserUpdate",
	"I_OnResumed",
	"I_OnChannelUpdate",
	"I_OnChannelPinsUpdate",
	"I_OnGuildBanAdd",
	"I_OnGuildBanRemove",
	"I_OnGuildEmojisUpdate",
	"I_OnGuildIntegrationsUpdate",
	"I_OnGuildMemberRemove",
	"I_OnGuildMemberUpdate",
	"I_OnGuildMembersChunk",
	"I_OnGuildRoleCreate",
	"I_OnGuildRoleUpdate",
	"I_OnGuildRoleDelete",
	"I_OnPresenceUpdateWS",
	"I_OnVoiceStateUpdate",
	"I_OnVoiceServerUpdate",
	"I_OnWebhooksUpdate",
	"I_END"
};

ModuleLoader::ModuleLoader(Bot* creator) : bot(creator)
{
	bot->core.log->info("Module loader initialising...");
}

ModuleLoader::~ModuleLoader()
{
}

/**
 * Attach an event to a module. Rather than just calling all events at all times, an event can be enabled or
 * disabled with Attach() and Detach(), this allows a module to programatically turn events on and off for itself.
 */
void ModuleLoader::Attach(const std::vector<Implementation> &i, Module* mod)
{
	for (auto n = i.begin(); n != i.end(); ++n) {
		if (std::find(EventHandlers[*n].begin(), EventHandlers[*n].end(), mod) == EventHandlers[*n].end()) {
			EventHandlers[*n].push_back(mod);
			bot->core.log->debug("Module \"{}\" attached event \"{}\"", mod->GetDescription(), StringNames[*n]);
		} else {
			bot->core.log->warn("Module \"{}\" is already attached to event \"{}\"", mod->GetDescription(), StringNames[*n]);
		}
	}
}

/**
 * Detach an event from a module, oppsite of Attach() above.
 */
void ModuleLoader::Detach(const std::vector<Implementation> &i, Module* mod)
{
	for (auto n = i.begin(); n != i.end(); ++n) {
		auto it = std::find(EventHandlers[*n].begin(), EventHandlers[*n].end(), mod);
		if (it != EventHandlers[*n].end()) {
			EventHandlers[*n].erase(it);
			bot->core.log->debug("Module \"{}\" detached event \"{}\"", mod->GetDescription(), StringNames[*n]);
		}
	}
}

/**
 * Return a reference to the module list
 */
const ModMap& ModuleLoader::GetModuleList() const
{
	return ModuleList;
}

/**
 * Load a module. Returns false on failure, true on success.
 * Sets the error message returned by GetLastError().
 */
bool ModuleLoader::Load(const std::string &filename)
{
	ModuleNative m;

	m.err = nullptr;
	m.dlopen_handle = nullptr;
	m.module_object = nullptr;
	m.init = nullptr;

	bot->core.log->info("Loading module \"{}\"", filename);

	std::lock_guard l(mtx);

	if (Modules.find(filename) == Modules.end()) {

		char buffer[PATH_MAX + 1];
		getcwd(buffer, PATH_MAX);
		std::string full_module_spec = std::string(buffer) + "/" + filename;

		m.dlopen_handle = dlopen(full_module_spec.c_str(), RTLD_NOW | RTLD_LOCAL);
		if (!m.dlopen_handle) {
			lasterror = dlerror();
			bot->core.log->error("Can't load module: {}", lasterror);
			return false;
		} else {
			if (!GetSymbol(m, "init_module")) {
				bot->core.log->error("Can't load module: {}", m.err ? m.err : "General error");
				lasterror = (m.err ? m.err : "General error");
				dlclose(m.dlopen_handle);
				return false;
			} else {
				bot->core.log->debug("Module shared object {} loaded, symbol found", filename);
				m.module_object = m.init(bot, this);
				/* In the event of a missing module_init symbol, dlsym() returns a valid pointer to a function that returns -1 as its pointer. Why? I don't know.
				 * FIXME find out why.
				*/
				if (!m.module_object || (uint64_t)m.module_object == 0xffffffffffffffff) {
					bot->core.log->error("Can't load module: Invalid module pointer returned. No symbol?");
					m.err = "Not a module (symbol init_module not found)";
					lasterror = m.err;
					dlclose(m.dlopen_handle);
					return false;
				} else {
					bot->core.log->debug("Module {} initialised", filename);
					Modules[filename] = m;
					ModuleList[filename] = m.module_object;
					lasterror = "";
					return true;
				}
			}
		}
	} else {
		bot->core.log->debug("Module {} already loaded!", filename);
		lasterror = "Module already loaded!";
		return false;
	}
}

/**
 * Returns the last error caused by Load() or Unload(), or an empty string for no error.
 */
const std::string& ModuleLoader::GetLastError()
{
	return lasterror;
}

/**
 * Unload a module from memory. Returns true on success or false on failure.
 */
bool ModuleLoader::Unload(const std::string &filename)
{
	std::lock_guard l(mtx);

	bot->core.log->debug("Unloading module {} ({}/{})", filename, Modules.size(), ModuleList.size());

	auto m = Modules.find(filename);

	if (m == Modules.end()) {
		lasterror = "Module is not loaded";
		return false;
	}

	ModuleNative& mod = m->second;

	/* Remove attached events */
	for (int j = I_BEGIN; j != I_END; ++j) {
		auto p = std::find(EventHandlers[j].begin(), EventHandlers[j].end(), mod.module_object);
		if (p != EventHandlers[j].end()) {
			EventHandlers[j].erase(p);
			bot->core.log->debug("Removed event {} from {}", StringNames[j], filename);
		}
	}
	/* Remove module entry */
	Modules.erase(m);
	
	auto v = ModuleList.find(filename);
	if (v != ModuleList.end()) {
		ModuleList.erase(v);
		bot->core.log->debug("Removed {} from module list", filename);
	}
	
	if (mod.module_object) {
		bot->core.log->debug("Module {} dtor", filename);
		delete mod.module_object;
	}
	
	/* Remove module from memory */
	if (mod.dlopen_handle) {
		bot->core.log->debug("Module {} dlclose()", filename);
		dlclose(mod.dlopen_handle);
	}

	bot->core.log->debug("New module counts: {}/{}", Modules.size(), ModuleList.size());

	return true;
}

/**
 * Unload, then reload a loaded module. Returns true on success or false on failure.
 * Failure to unload causes load to be skipped, so you can't use this function to load
 * a module that isnt loaded.
 */
bool ModuleLoader::Reload(const std::string &filename)
{
	/* Short-circuit evaluation here means that if Unload() returns false,
	 * Load() won't be called at all.
	 */
	return (Unload(filename) && Load(filename));
}

/**
 * Load all modules from the config file modules.json
 */
void ModuleLoader::LoadAll()
{
	json document;
	std::ifstream configfile("../config.json");
	configfile >> document;
	json modlist = document["modules"];
	for (auto entry = modlist.begin(); entry != modlist.end(); ++entry) {
		std::string modulename = entry->get<std::string>();
		this->Load(modulename);
	}
}

/**
 * Return a given symbol name from a shared object represented by the ModuleNative value.
 */
bool ModuleLoader::GetSymbol(ModuleNative &native, const char *sym_name)
{
	/* Find exported symbol in shared object */
	if (native.dlopen_handle) {
		dlerror(); // clear value
		native.init = (initfunctype*)dlsym(native.dlopen_handle, sym_name);
		native.err = dlerror();
		//printf("dlopen_handle=0x%016x, native.init=0x%016x native.err=\"%s\" dlsym=0x%016x sym_name=%s\n", native.dlopen_handle, native.init, native.err ? native.err : "<NULL>", dlsym(native.dlopen_handle, sym_name), sym_name);
		if (!native.init || native.err) {
			return false;
		}
	} else {
		native.err = "ModuleLoader::GetSymbol(): Invalid dlopen() handle";
		return false;
	}
	return true;
}

Module::Module(Bot* instigator, ModuleLoader* ml) : bot(instigator)
{
}

Module::~Module()
{
}

std::string Module::GetVersion()
{
	return "";
}

std::string Module::GetDescription()
{
	return "";
}

bool Module::OnChannelCreate(const modevent::channel_create &channel)
{
	return true;
} 

bool Module::OnReady(const modevent::ready &ready)
{
	return true;
}

bool Module::OnChannelDelete(const modevent::channel_delete &channel)
{
	return true;
}

bool Module::OnGuildCreate(const modevent::guild_create &guild)
{
	return true;
}

bool Module::OnGuildDelete(const modevent::guild_delete &guild)
{
	return true;
}

bool Module::OnGuildMemberAdd(const modevent::guild_member_add &gma)
{
	return true;
}

bool Module::OnMessage(const modevent::message_create &message, const std::string& clean_message, bool mentioned, const std::vector<std::string> &stringmentions)
{
	return true;
}

bool Module::OnPresenceUpdate()
{
	return true;
}

bool Module::OnRestEnd(std::chrono::steady_clock::time_point start_time, uint16_t code)
{
	return true;
}

bool Module::OnTypingStart(const modevent::typing_start &obj)
{
	return true;
}


bool Module::OnMessageUpdate(const modevent::message_update &obj)
{
	return true;
}


bool Module::OnMessageDelete(const modevent::message_delete &obj)
{
	return true;
}


bool Module::OnMessageDeleteBulk(const modevent::message_delete_bulk &obj)
{
	return true;
}


bool Module::OnGuildUpdate(const modevent::guild_update &obj)
{
	return true;
}


bool Module::OnMessageReactionAdd(const modevent::message_reaction_add &obj)
{
	return true;
}


bool Module::OnMessageReactionRemove(const modevent::message_reaction_remove &obj)
{
	return true;
}


bool Module::OnMessageReactionRemoveAll(const modevent::message_reaction_remove_all &obj)
{
	return true;
}


bool Module::OnUserUpdate(const modevent::user_update &obj)
{
	return true;
}


bool Module::OnResumed(const modevent::resumed &obj)
{
	return true;
}


bool Module::OnChannelUpdate(const modevent::channel_update &obj)
{
	return true;
}


bool Module::OnChannelPinsUpdate(const modevent::channel_pins_update &obj)
{
	return true;
}


bool Module::OnGuildBanAdd(const modevent::guild_ban_add &obj)
{
	return true;
}


bool Module::OnGuildBanRemove(const modevent::guild_ban_remove &obj)
{
	return true;
}


bool Module::OnGuildEmojisUpdate(const modevent::guild_emojis_update &obj)
{
	return true;
}


bool Module::OnGuildIntegrationsUpdate(const modevent::guild_integrations_update &obj)
{
	return true;
}


bool Module::OnGuildMemberRemove(const modevent::guild_member_remove &obj)
{
	return true;
}


bool Module::OnGuildMemberUpdate(const modevent::guild_member_update &obj)
{
	return true;
}


bool Module::OnGuildMembersChunk(const modevent::guild_members_chunk &obj)
{
	return true;
}


bool Module::OnGuildRoleCreate(const modevent::guild_role_create &obj)
{
	return true;
}


bool Module::OnGuildRoleUpdate(const modevent::guild_role_update &obj)
{
	return true;
}


bool Module::OnGuildRoleDelete(const modevent::guild_role_delete &obj)
{
	return true;
}


bool Module::OnPresenceUpdateWS(const modevent::presence_update &obj)
{
	return true;
}


bool Module::OnVoiceStateUpdate(const modevent::voice_state_update &obj)
{
	return true;
}


bool Module::OnVoiceServerUpdate(const modevent::voice_server_update &obj)
{
	return true;
}


bool Module::OnWebhooksUpdate(const modevent::webhooks_update &obj)
{
	return true;
}

bool Module::OnAllShardsReady()
{
	return true;
}


/**
 * Output a simple embed to a channel consisting just of a message.
 */
void Module::EmbedSimple(const std::string &message, int64_t channelID)
{
	std::stringstream s;
	json embed_json;

	s << "{\"color\":16767488, \"description\": \"" << message << "\"}";

	try {
		embed_json = json::parse(s.str());
	}
	catch (const std::exception &e) {
		bot->core.log->error("Invalid json for channel {} created by EmbedSimple: ", channelID, s.str());
	}
	aegis::channel* channel = bot->core.find_channel(channelID);
	if (channel) {
		if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == channel->get_guild().get_id()) {
			channel->create_message_embed("", embed_json);
		}
	} else {
		bot->core.log->error("Invalid channel {} passed to EmbedSimple", channelID);
	}
}
