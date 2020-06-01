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

#include <sporks/bot.h>
#include <sporks/modules.h>
#include <sporks/stringops.h>
#include <sporks/database.h>
#include <sporks/config.h>
#include <sstream>
#include <thread>

/**
 * Provides caching of users, guilds and memberships to an sql database for use by external programs.
 */

class SQLCacheModule : public Module
{
	/* Queue processing threads */
	std::thread* thr_userqueue;
	std::thread* thr_guildqueue;

	/* Safety mutexes */
	std::mutex user_cache_mutex;
	std::mutex guild_cache_mutex;

	/* True if the thread is to terminate */
	bool terminate;

	/* Userqueue: a queue of users waiting to be written to SQL for the dashboard */
	std::queue<aegis::gateway::objects::user> userqueue;
	std::queue<aegis::gateway::objects::guild> guildqueue;
public:

	void SaveCachedUsersThread() {
		time_t last_message = time(NULL);
		aegis::gateway::objects::user u;
		while (!this->terminate) {
			if (!userqueue.empty()) {
				{
					std::lock_guard<std::mutex> user_cache_lock(user_cache_mutex);
					u = userqueue.front();
					userqueue.pop();
					bot->counters["userqueue"] = userqueue.size();
				};
				std::string bot = u.is_bot() ? "1" : "0";
				db::query("INSERT INTO infobot_discord_user_cache (id, username, discriminator, avatar, bot) VALUES(?, '?', '?', '?', ?) ON DUPLICATE KEY UPDATE username = '?', discriminator = '?', avatar = '?'", {u.id.get(), u.username, u.discriminator, u.avatar, bot, u.username, u.discriminator, u.avatar});
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			} else {
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
			if (time(NULL) > last_message) {
				if (userqueue.size() > 0) {
					bot->core.log->info("User queue size: {} objects", userqueue.size());
				}
				last_message = time(NULL) + 60;
			}
		}
	}

	void SaveCachedGuildsThread() {
		time_t last_message = time(NULL);
		aegis::gateway::objects::guild gc;
		while (!this->terminate) {
			if (!guildqueue.empty()) {
				{
					std::lock_guard<std::mutex> user_cache_lock(guild_cache_mutex);
					gc = guildqueue.front();
					guildqueue.pop();
					bot->counters["guildqueue"] = guildqueue.size();
				};
				for (auto i = gc.channels.begin(); i != gc.channels.end(); ++i) {
					getSettings(bot, i->id.get(), gc.id.get());
				}
				for (auto i = gc.members.begin(); i != gc.members.end(); ++i) {
					std::lock_guard<std::mutex> user_cache_lock(user_cache_mutex);
					userqueue.push(i->_user);
					bot->counters["userqueue"] = userqueue.size();
					std::string roles_str;
					for (auto n = i->roles.begin(); n != i->roles.end(); ++n) {
						roles_str.append(std::to_string(n->get())).append(",");
					}
					roles_str = roles_str.substr(0, roles_str.length() - 1);
					std::string dashboard = "0";
					if (gc.owner_id == i->_user.id) {
						/* Server owner */
						dashboard = "1";
					}
					db::query("INSERT INTO infobot_membership (member_id, guild_id, nick, roles, dashboard) VALUES(?, ?, '?', '?','?') ON DUPLICATE KEY UPDATE nick = '?', roles = '?', dashboard = '?'", {i->_user.id.get(), gc.id.get(), i->nick, roles_str, dashboard, i->nick, roles_str, dashboard});
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			} else {
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
			if (time(NULL) > last_message) {
				if (guildqueue.size() > 0) {
					bot->core.log->info("User guild size: {} objects", guildqueue.size());
				}
				last_message = time(NULL) + 60;
			}
		}
	}

	SQLCacheModule(Bot* instigator, ModuleLoader* ml) : Module(instigator, ml), thr_userqueue(nullptr), thr_guildqueue(nullptr), terminate(false)
	{
		ml->Attach({ I_OnGuildCreate, I_OnPresenceUpdate, I_OnGuildMemberAdd, I_OnChannelCreate, I_OnChannelDelete, I_OnGuildDelete, I_OnGuildMemberRemove }, this);
		bot->counters["userqueue"] = 0;
		thr_userqueue = new std::thread(&SQLCacheModule::SaveCachedUsersThread, this);
		thr_guildqueue = new std::thread(&SQLCacheModule::SaveCachedGuildsThread, this);
	}

	virtual ~SQLCacheModule()
	{
		terminate = true;
		bot->DisposeThread(thr_userqueue);
		bot->DisposeThread(thr_guildqueue);
		bot->counters["userqueue"] = 0;
		bot->counters["guildqueue"] = 0;
	}

	virtual std::string GetVersion()
	{
		/* NOTE: This version string below is modified by a pre-commit hook on the git repository */
		std::string version = "$ModVer 8$";
		return "1.0." + version.substr(8,version.length() - 9);
	}

	virtual std::string GetDescription()
	{
		return "User/Guild SQL Cache";
	}

	virtual bool OnGuildCreate(const modevent::guild_create &gc)
	{
		db::query("INSERT INTO infobot_shard_map (guild_id, shard_id, name, icon, unavailable, owner_id) VALUES('?','?','?','?','?','?') ON DUPLICATE KEY UPDATE shard_id = '?', name = '?', icon = '?', unavailable = '?', owner_id = '?'",
			{
				gc.guild.id.get(),
				gc.shard.get_id(),
				gc.guild.name,
				gc.guild.icon,
				gc.guild.unavailable,
				gc.guild.owner_id.get(),
				gc.shard.get_id(),
				gc.guild.name,
				gc.guild.icon,
				gc.guild.unavailable,
				gc.guild.owner_id.get()
			}
		);

		{
			std::lock_guard<std::mutex> guild_cache_lock(guild_cache_mutex);
			guildqueue.push(gc.guild);
			bot->counters["guildqueue"] = guildqueue.size();
		}

		return true;
	}

	virtual bool OnPresenceUpdate()
	{
		const aegis::shards::shard_mgr& s = bot->core.get_shard_mgr();
		const std::vector<std::unique_ptr<aegis::shards::shard>>& shards = s.get_shards();
		for (auto i = shards.begin(); i != shards.end(); ++i) {
			const aegis::shards::shard* shard = i->get();
			db::query("INSERT INTO infobot_shard_status (id, connected, online, uptime, transfer, transfer_compressed) VALUES('?','?','?','?','?','?') ON DUPLICATE KEY UPDATE connected = '?', online = '?', uptime = '?', transfer = '?', transfer_compressed = '?'",
				{
					shard->get_id(),
					shard->is_connected(),
					shard->is_online(),
					shard->uptime(),
					shard->get_transfer_u(),
					shard->get_transfer(),
					shard->is_connected(),
					shard->is_online(),
					shard->uptime(),
					shard->get_transfer_u(),
					shard->get_transfer()
				}
			);
		}
		return true;
	}

	virtual bool OnGuildMemberRemove(const modevent::guild_member_remove &gmr)
	{
		db::query("DELETE FROM infobot_membership WHERE member_id = '?'", {gmr.user.id.get()});
		return true;
	}

	virtual bool OnGuildMemberAdd(const modevent::guild_member_add &gma)
	{
		bool _bot = gma.member._user.is_bot();
		std::string roles_str;
		aegis::guild* g = bot->core.find_guild(gma.member.guild_id.get());
		for (auto n = gma.member.roles.begin(); n != gma.member.roles.end(); ++n) {
			roles_str.append(std::to_string(n->get())).append(",");
		}
		roles_str = roles_str.substr(0, roles_str.length() - 1);
		std::string dashboard = "0";
		if (g->get_owner() == gma.member._user.id) {
			/* Server owner */
			dashboard = "1";
		}		
		db::query("INSERT INTO infobot_discord_user_cache (id, username, discriminator, avatar, bot) VALUES(?, '?', '?', '?', ?) ON DUPLICATE KEY UPDATE username = '?', discriminator = '?', avatar = '?'", {gma.member._user.id.get(), gma.member._user.username, gma.member._user.discriminator, gma.member._user.avatar, _bot, gma.member._user.username, gma.member._user.discriminator, gma.member._user.avatar});
		db::query("INSERT INTO infobot_membership (member_id, guild_id, nick, roles, dashboard) VALUES(?, ?, '?', '?','?') ON DUPLICATE KEY UPDATE nick = '?', roles = '?', dashboard = '?'", {gma.member._user.id.get(), gma.member.guild_id.get(), gma.member.nick, roles_str, dashboard, gma.member.nick, roles_str, dashboard});
		return true;
	}

	virtual bool OnChannelCreate(const modevent::channel_create channel_create)
	{
		getSettings(bot, channel_create.channel.id.get(), channel_create.channel.guild_id.get());
		return true;
	}

	virtual bool OnChannelDelete(const modevent::channel_delete cd)
	{
		db::query("DELETE FROM infobot_discord_settings WHERE id = '?'", {cd.channel.id.get()});
		return true;
	}

	virtual bool OnGuildDelete(const modevent::guild_delete gd)
	{
		db::query("DELETE FROM infobot_discord_settings WHERE guild_id = '?'", {gd.guild_id.get()});
		db::query("DELETE FROM infobot_shard_map WHERE guild_id = '?'", {gd.guild_id.get()});
		db::query("DELETE FROM infobot_membership WHERE guild_id = '?'", {gd.guild_id.get()});
		return true;
	}
};

ENTRYPOINT(SQLCacheModule);

