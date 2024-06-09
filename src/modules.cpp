/************************************************************************************
 * 
 * TriviaBot, the Discord Quiz Bot with over 80,000 questions!
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

#include <dpp/dpp.h>
#include <dpp/nlohmann/json.hpp>
#include <fmt/format.h>
#include <sporks/modules.h>
#ifndef _GNU_SOURCE
	#define _GNU_SOURCE
#endif
#include <unistd.h>
#include <limits.h>
#include <link.h>
#include <dlfcn.h>
#include <sstream>
#include <sporks/stringops.h>

using json = nlohmann::json;

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
	bot->core->log(dpp::ll_info, "Module loader initialising...");
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
			bot->core->log(dpp::ll_debug, fmt::format("Module \"{}\" attached event \"{}\"", mod->GetDescription(), StringNames[*n]));
		} else {
			bot->core->log(dpp::ll_warning, fmt::format("Module \"{}\" is already attached to event \"{}\"", mod->GetDescription(), StringNames[*n]));
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
			bot->core->log(dpp::ll_debug, fmt::format("Module \"{}\" detached event \"{}\"", mod->GetDescription(), StringNames[*n]));
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

	bot->core->log(dpp::ll_info, fmt::format("Loading module \"{}\"", filename));

	std::lock_guard l(mtx);

	if (Modules.find(filename) == Modules.end()) {

		char buffer[PATH_MAX + 1];
		std::string full_module_spec = std::string(getcwd(buffer, PATH_MAX)) + "/" + filename;

		m.dlopen_handle = dlopen(full_module_spec.c_str(), RTLD_NOW | RTLD_LOCAL);
		if (!m.dlopen_handle) {
			lasterror = dlerror();
			bot->core->log(dpp::ll_error, fmt::format("Can't load module: {}", lasterror));
			return false;
		} else {
			if (!GetSymbol(m, "init_module")) {
				bot->core->log(dpp::ll_error, fmt::format("Can't load module: {}", m.err ? m.err : "General error"));
				lasterror = (m.err ? m.err : "General error");
				dlclose(m.dlopen_handle);
				return false;
			} else {
				bot->core->log(dpp::ll_debug, fmt::format("Module shared object {} loaded, symbol found", filename));
				m.module_object = m.init(bot, this);
				/* In the event of a missing module_init symbol, dlsym() returns a valid pointer to a function that returns -1 as its pointer. Why? I don't know.
				 * FIXME find out why.
				*/
				if (!m.module_object || (uint64_t)m.module_object == 0xffffffffffffffff) {
					bot->core->log(dpp::ll_error, fmt::format("Can't load module: Invalid module pointer returned. No symbol?"));
					m.err = "Not a module (symbol init_module not found)";
					lasterror = m.err;
					dlclose(m.dlopen_handle);
					return false;
				} else {
					bot->core->log(dpp::ll_debug, fmt::format("Module {} initialised", filename));
					Modules[filename] = m;
					ModuleList[filename] = m.module_object;
					lasterror = "";
					return true;
				}
			}
		}
	} else {
		bot->core->log(dpp::ll_error, fmt::format("Module {} already loaded!", filename));
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

	bot->core->log(dpp::ll_info, fmt::format("Unloading module {} ({}/{})", filename, Modules.size(), ModuleList.size()));

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
			bot->core->log(dpp::ll_debug, fmt::format("Removed event {} from {}", StringNames[j], filename));
		}
	}
	/* Remove module entry */
	Modules.erase(m);
	
	auto v = ModuleList.find(filename);
	if (v != ModuleList.end()) {
		ModuleList.erase(v);
		bot->core->log(dpp::ll_debug, fmt::format("Removed {} from module list", filename));
	}
	
	if (mod.module_object) {
		bot->core->log(dpp::ll_debug, fmt::format("Module {} dtor", filename));
		delete mod.module_object;
	}
	
	/* Remove module from memory */
	if (mod.dlopen_handle) {
		bot->core->log(dpp::ll_debug, fmt::format("Module {} dlclose()", filename));
		dlclose(mod.dlopen_handle);
	}

	bot->core->log(dpp::ll_debug, fmt::format("New module counts: {}/{}", Modules.size(), ModuleList.size()));

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

bool Module::OnChannelCreate(const dpp::channel_create_t &channel)
{
	return true;
} 

bool Module::OnReady(const dpp::ready_t &ready)
{
	return true;
}

bool Module::OnChannelDelete(const dpp::channel_delete_t &channel)
{
	return true;
}

bool Module::OnGuildCreate(const dpp::guild_create_t &guild)
{
	return true;
}

bool Module::OnGuildDelete(const dpp::guild_delete_t &guild)
{
	return true;
}

bool Module::OnGuildMemberAdd(const dpp::guild_member_add_t &gma)
{
	return true;
}

bool Module::OnMessage(const dpp::message_create_t &message, const std::string& clean_message, bool mentioned, const std::vector<std::string> &stringmentions)
{
	return true;
}

bool Module::OnPresenceUpdate()
{
	return true;
}

bool Module::OnTypingStart(const dpp::typing_start_t &obj)
{
	return true;
}


bool Module::OnMessageUpdate(const dpp::message_update_t &obj)
{
	return true;
}


bool Module::OnMessageDelete(const dpp::message_delete_t &obj)
{
	return true;
}


bool Module::OnMessageDeleteBulk(const dpp::message_delete_bulk_t &obj)
{
	return true;
}


bool Module::OnGuildUpdate(const dpp::guild_update_t &obj)
{
	return true;
}


bool Module::OnMessageReactionAdd(const dpp::message_reaction_add_t &obj)
{
	return true;
}


bool Module::OnMessageReactionRemove(const dpp::message_reaction_remove_t &obj)
{
	return true;
}


bool Module::OnMessageReactionRemoveAll(const dpp::message_reaction_remove_all_t &obj)
{
	return true;
}


bool Module::OnUserUpdate(const dpp::user_update_t &obj)
{
	return true;
}


bool Module::OnResumed(const dpp::resumed_t &obj)
{
	return true;
}


bool Module::OnChannelUpdate(const dpp::channel_update_t &obj)
{
	return true;
}


bool Module::OnChannelPinsUpdate(const dpp::channel_pins_update_t &obj)
{
	return true;
}


bool Module::OnGuildBanAdd(const dpp::guild_ban_add_t &obj)
{
	return true;
}


bool Module::OnGuildBanRemove(const dpp::guild_ban_remove_t &obj)
{
	return true;
}


bool Module::OnGuildEmojisUpdate(const dpp::guild_emojis_update_t &obj)
{
	return true;
}


bool Module::OnGuildIntegrationsUpdate(const dpp::guild_integrations_update_t &obj)
{
	return true;
}


bool Module::OnGuildMemberRemove(const dpp::guild_member_remove_t &obj)
{
	return true;
}


bool Module::OnGuildMemberUpdate(const dpp::guild_member_update_t &obj)
{
	return true;
}


bool Module::OnGuildMembersChunk(const dpp::guild_members_chunk_t &obj)
{
	return true;
}


bool Module::OnGuildRoleCreate(const dpp::guild_role_create_t &obj)
{
	return true;
}


bool Module::OnGuildRoleUpdate(const dpp::guild_role_update_t &obj)
{
	return true;
}


bool Module::OnGuildRoleDelete(const dpp::guild_role_delete_t &obj)
{
	return true;
}


bool Module::OnPresenceUpdateWS(const dpp::presence_update_t &obj)
{
	return true;
}


bool Module::OnVoiceStateUpdate(const dpp::voice_state_update_t &obj)
{
	return true;
}


bool Module::OnVoiceServerUpdate(const dpp::voice_server_update_t &obj)
{
	return true;
}


bool Module::OnWebhooksUpdate(const dpp::webhooks_update_t &obj)
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
void Module::EmbedSimple(const std::string &message, uint64_t channelID, uint64_t guildID)
{
	std::stringstream s;
	json embed_json;

	s << "{\"color\":16767488, \"description\": \"" << message << "\"}";

	try {
		embed_json = json::parse(s.str());
	}
	catch (const std::exception &e) {
		bot->core->log(dpp::ll_error, fmt::format("Invalid json for channel {} created by EmbedSimple: ", channelID, s.str()));
	}
	if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == guildID) {
		dpp::message m;
		m.channel_id = channelID;
		m.embeds.push_back(dpp::embed(&embed_json));
		bot->core->message_create(m);
	}
}
