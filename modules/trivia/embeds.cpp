/************************************************************************************
 * 
 * TriviaBot, The trivia bot for discord based on Fruitloopy Trivia for ChatSpike IRC
 *
 * Copyright 2004 Craig Edwards <support@brainbox.cc>
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

#include <dpp/dpp.h>
#include <fmt/format.h>
#include <dpp/nlohmann/json.hpp>
#include <string>
#include <cstdint>
#include <fstream>
#include <streambuf>
#include <sporks/modules.h>
#include <sporks/stringops.h>
#include <sporks/statusfield.h>
#include <sporks/database.h>
#include "trivia.h"
#include "webrequest.h"

using json = nlohmann::json;

/* Make a string safe to send as a JSON literal */
std::string TriviaModule::escape_json(const std::string &s) {
	std::ostringstream o;
	for (auto c = s.cbegin(); c != s.cend(); c++) {
		switch (*c) {
		case '"': o << "\\\""; break;
		case '\\': o << "\\\\"; break;
		case '\b': o << "\\b"; break;
		case '\f': o << "\\f"; break;
		case '\n': o << "\\n"; break;
		case '\r': o << "\\r"; break;
		case '\t': o << "\\t"; break;
		default:
			if ('\x00' <= *c && *c <= '\x1f') {
				o << "\\u"
				  << std::hex << std::setw(4) << std::setfill('0') << (int)*c;
			} else {
				o << *c;
			}
		}
	}
	return o.str();
}

/* Create an embed from a JSON string and send it to a channel */
void TriviaModule::ProcessEmbed(const guild_settings_t& settings, const std::string &embed_json, dpp::snowflake channelID)
{
	ProcessEmbed("", 0, settings, embed_json, channelID);
}

/* Create an embed from a JSON string and send it to a channel */
void TriviaModule::ProcessEmbed(const std::string& interaction_token, dpp::snowflake command_id, const guild_settings_t& settings, const std::string &embed_json, dpp::snowflake channelID)
{
	json embed;
	std::string cleaned_json = embed_json;
	/* Put unicode zero-width spaces in @everyone and @here */
	cleaned_json = ReplaceString(cleaned_json, "@everyone", "@‎everyone");
	cleaned_json = ReplaceString(cleaned_json, "@here", "@‎here");
	try {
		/* Tabs to spaces */
		cleaned_json = ReplaceString(cleaned_json, "\t", " ");
		embed = json::parse(cleaned_json);
	}
	catch (const std::exception &e) {
		if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == settings.guild_id) {
			try {

				if (!interaction_token.empty() && command_id != 0) {
					dpp::message msg(channelID, fmt::format(_("EMBED_ERROR_1", settings), cleaned_json, e.what()));
					msg.guild_id = settings.guild_id;
					msg.channel_id = channelID;
					bot->core->interaction_response_edit(interaction_token, msg);
				} else {
					bot->core->message_create(dpp::message(channelID, fmt::format(_("EMBED_ERROR_1", settings), cleaned_json, e.what())));
				}
			}
			catch (const std::exception &e) {
				bot->core->log(dpp::ll_error, fmt::format("MALFORMED UNICODE: {}", e.what()));
			}
			bot->sent_messages++;
		}
	}
	if (!bot->IsTestMode() || from_string<uint64_t>(Bot::GetConfig("test_server"), std::dec) == settings.guild_id) {

		if (!interaction_token.empty() && command_id != 0) {
			dpp::message msg;			
			msg.content = "";
			msg.guild_id = settings.guild_id;
			msg.channel_id = channelID;
			msg.add_embed(dpp::embed(&embed));
			bot->core->interaction_response_edit(interaction_token, msg, [this](const dpp::confirmation_callback_t &callback) {
				if (callback.is_error()) {
					this->bot->core->log(dpp::ll_error, fmt::format("Can't edit interaction response: {}", callback.http_info.body));
				}
			});
			bot->sent_messages++;
			return;
		}

		/* Check if this channel has a webhook. If it does, use it! */
		std::string webhook_id;
		{
			std::lock_guard<std::mutex> lock(this->wh_mutex);
			auto i = this->webhooks.find(channelID);
			if (i != this->webhooks.end()) {
				webhook_id = i->second;
			}
		}
		if (webhook_id.empty()) {
			db::resultset rs = db::query("SELECT * FROM channel_webhooks WHERE channel_id = ?", {channelID});
			if (rs.size()) {
				std::lock_guard<std::mutex> lock(this->wh_mutex);
				webhook_id = rs[0]["webhook"];
				this->webhooks[channelID] = webhook_id;
			}
		}
		if (!webhook_id.empty()) {
			PostWebhook(webhook_id, cleaned_json, channelID);
		} else {
			bot->core->message_create(dpp::message(channelID, dpp::embed(&embed)));
		}
		bot->sent_messages++;
	}
}

void TriviaModule::SimpleEmbed(const guild_settings_t& settings, const std::string &emoji, const std::string &text, dpp::snowflake channelID, const std::string &title, const std::string &image, const std::string &thumbnail)
{
	SimpleEmbed("", 0, settings, emoji, text, channelID, title, image,thumbnail);
}

void TriviaModule::SimpleEmbed(const std::string& interaction_token, dpp::snowflake command_id, const guild_settings_t& settings, const std::string &emoji, const std::string &text, dpp::snowflake channelID, const std::string &title, const std::string &image, const std::string &thumbnail)
{
	uint32_t colour = settings.embedcolour;
	std::string imageinfo;
	/* Add image if there is one */
	if (!image.empty()) {
		imageinfo += ",\"image\":{\"url\":\"" + escape_json(image) + "\"}";
	}
	if (!thumbnail.empty()) {
		imageinfo += ",\"thumbnail\":{\"url\":\"" + escape_json(thumbnail) + "\"}";
	}
	imageinfo += ",\"footer\":{\"text\":\"" + escape_json(_("POWERED_BY", settings)) + "\",\"icon_url\":\"https://triviabot.co.uk/images/triviabot_tl_icon.png\"}";
	if (!title.empty()) {
		/* With title */
		ProcessEmbed(interaction_token, command_id, settings, fmt::format("{{\"title\":\"{}\",\"color\":{},\"description\":\"{} {}\"{}}}", escape_json(title), colour, emoji, escape_json(text), imageinfo), channelID);
	} else {
		/* Without title */
		ProcessEmbed(interaction_token, command_id, settings, fmt::format("{{\"color\":{},\"description\":\"{} {}\"{}}}", colour, emoji, escape_json(text), imageinfo), channelID);
	}
}

/* Send an embed containing one or more fields */
void TriviaModule::EmbedWithFields(const guild_settings_t& settings, const std::string &title, std::vector<field_t> fields, dpp::snowflake channelID, const std::string &url, const std::string &image, const std::string &thumbnail)
{
	EmbedWithFields("", 0, settings, title, fields, channelID, url, image, thumbnail);
}

/* Send an embed containing one or more fields */
void TriviaModule::EmbedWithFields(const std::string& interaction_token, dpp::snowflake command_id, const guild_settings_t& settings, const std::string &title, std::vector<field_t> fields, dpp::snowflake channelID, const std::string &url, const std::string &image, const std::string &thumbnail)
{
		uint32_t colour = settings.embedcolour;
		std::string json = fmt::format("{{" + (!url.empty() ? "\"url\":\"" + escape_json(url) + "\"," : "") + "\"title\":\"{}\",\"color\":{},\"fields\":[", escape_json(title), colour);
		for (auto v = fields.begin(); v != fields.end(); ++v) {
			json += fmt::format("{{\"name\":\"{}\",\"value\":\"{}\",\"inline\":{}}}", escape_json(v->name), escape_json(v->value), v->_inline ? "true" : "false");
			auto n = v;
			if (++n != fields.end()) {
				json += ",";
			}
		}
		json += "],";
		/* Add image if there is one */
		if (!image.empty()) {
			json += "\"image\":{\"url\":\"" + escape_json(image) + "\"},";
		}
		if (!thumbnail.empty()) {
			json += "\"thumbnail\":{\"url\":\"" + escape_json(thumbnail) + "\"},";
		}
		/* Footer, 'powered by' detail, icon */
		json += "\"footer\":{\"link\":\"https://triviabot.co.uk/\",\"text\":\"" + _("POWERED_BY", settings) + "\",\"icon_url\":\"https:\\/\\/triviabot.co.uk\\/images\\/triviabot_tl_icon.png\"}}";
		ProcessEmbed(interaction_token, command_id, settings, json, channelID);
}

