-- NOTE: These tables are only those needed to run the client-side (bot)
-- There are many other tables needed to run the API side.

SET SQL_MODE = "NO_AUTO_VALUE_ON_ZERO";
SET AUTOCOMMIT = 0;
START TRANSACTION;
SET time_zone = "+00:00";

CREATE TABLE `bot_guild_settings` (
  `snowflake_id` bigint(20) UNSIGNED NOT NULL COMMENT 'PK',
  `prefix` varchar(64) NOT NULL DEFAULT '!' COMMENT 'Server''s prefix',
  `embedcolour` int(10) UNSIGNED NOT NULL DEFAULT 3238819 COMMENT 'Colour used in embeds',
  `moderator_roles` text DEFAULT NULL COMMENT 'Space seperated list of moderator role IDs',
  `premium` tinyint(1) UNSIGNED NOT NULL DEFAULT 0 COMMENT 'True if premium guild',
  `only_mods_stop` tinyint(1) UNSIGNED NOT NULL DEFAULT 0 COMMENT 'True if only moderators can do !trivia stop',
  `role_reward_enabled` tinyint(1) UNSIGNED NOT NULL DEFAULT 0 COMMENT 'True if role rewards are enabled',
  `role_reward_id` bigint(20) UNSIGNED DEFAULT NULL COMMENT 'Role snowflake ID',
  `custom_css` longtext DEFAULT NULL COMMENT 'Custom Stats CSS for premium users only',
  `custom_url` varchar(250) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Stores guild specific settings';

CREATE TABLE `infobot_bandwidth` (
  `logdate` timestamp NOT NULL DEFAULT current_timestamp(),
  `kbps_in` double NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE `infobot_discord_counts` (
  `shard_id` bigint(20) UNSIGNED NOT NULL COMMENT 'Shard ID',
  `dev` tinyint(1) UNSIGNED NOT NULL DEFAULT 0 COMMENT 'true if development data',
  `user_count` bigint(20) NOT NULL,
  `server_count` bigint(20) NOT NULL,
  `shard_count` bigint(20) UNSIGNED NOT NULL DEFAULT 1 COMMENT 'number of shards',
  `channel_count` bigint(20) UNSIGNED NOT NULL,
  `sent_messages` bigint(20) UNSIGNED NOT NULL DEFAULT 0,
  `received_messages` bigint(20) UNSIGNED NOT NULL DEFAULT 0,
  `memory_usage` bigint(20) UNSIGNED NOT NULL DEFAULT 0,
  `last_updated` datetime NOT NULL DEFAULT current_timestamp() ON UPDATE current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COMMENT='Counts of users/servers on a per-shard basis';

CREATE TABLE `infobot_discord_list_sites` (
  `id` bigint(20) UNSIGNED NOT NULL,
  `name` varchar(255) NOT NULL,
  `url` varchar(255) NOT NULL,
  `server_count_field` varchar(255) NOT NULL,
  `user_count_field` varchar(255) DEFAULT NULL,
  `shard_count_field` varchar(255) DEFAULT NULL,
  `sent_message_count_field` varchar(255) DEFAULT NULL,
  `received_message_count_field` varchar(255) DEFAULT NULL,
  `ram_used_field` varchar(255) DEFAULT NULL,
  `channels_field` varchar(255) DEFAULT NULL,
  `auth_field` varchar(100) DEFAULT NULL,
  `authorization` varchar(255) NOT NULL,
  `post_type` enum('json','post') NOT NULL DEFAULT 'json'
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

CREATE TABLE `infobot_shard_status` (
  `id` int(10) UNSIGNED NOT NULL,
  `connected` tinyint(1) UNSIGNED NOT NULL,
  `online` tinyint(1) UNSIGNED NOT NULL,
  `uptime` bigint(20) UNSIGNED NOT NULL,
  `transfer` bigint(20) UNSIGNED NOT NULL,
  `transfer_compressed` bigint(20) UNSIGNED NOT NULL,
  `uptimerobot_heartbeat` varchar(300) DEFAULT NULL,
  `updated` timestamp NULL DEFAULT current_timestamp() ON UPDATE current_timestamp(),
  `uptimerobot_response` varchar(255) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Contains details of all active shards';

CREATE TABLE `infobot_votes` (
  `id` bigint(20) UNSIGNED NOT NULL,
  `snowflake_id` bigint(20) UNSIGNED NOT NULL,
  `vote_time` timestamp NULL DEFAULT current_timestamp(),
  `origin` varchar(50) NOT NULL,
  `dm_hints` tinyint(2) UNSIGNED NOT NULL DEFAULT 8
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE `infobot_vote_counters` (
  `snowflake_id` bigint(20) UNSIGNED NOT NULL,
  `vote_count` bigint(20) UNSIGNED NOT NULL DEFAULT 0,
  `last_vote` datetime NOT NULL DEFAULT current_timestamp() ON UPDATE current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Counters of votes cast';

CREATE TABLE `infobot_vote_links` (
  `site` varchar(80) NOT NULL,
  `vote_url` varchar(256) NOT NULL,
  `sortorder` float NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Voting URLs for the sites which have webhooks';

CREATE TABLE `premium_credits` (
  `user_id` bigint(20) UNSIGNED NOT NULL COMMENT 'Discord snowflake ID',
  `subscription_id` varchar(255) CHARACTER SET ascii NOT NULL COMMENT 'Subscription ID from chargebee',
  `guild_id` bigint(20) UNSIGNED DEFAULT NULL COMMENT 'Guild ID subscription is attached to, if any',
  `active` tinyint(1) UNSIGNED NOT NULL DEFAULT 1 COMMENT 'True if the subscription is currently active',
  `since` datetime NOT NULL DEFAULT current_timestamp() COMMENT 'Date of subscription start',
  `plan_id` varchar(250) CHARACTER SET ascii NOT NULL COMMENT 'Plan associated with the subscription',
  `cancel_date` datetime DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE `premium_plans` (
  `id` varchar(250) CHARACTER SET ascii NOT NULL COMMENT 'From chargebee',
  `lifetime` tinyint(1) UNSIGNED NOT NULL DEFAULT 0 COMMENT 'True if the plan is charged only once',
  `name` varchar(250) NOT NULL COMMENT 'Plan name',
  `price` decimal(5,2) UNSIGNED NOT NULL COMMENT 'Price',
  `period` int(10) UNSIGNED NOT NULL,
  `period_unit` enum('year','month') NOT NULL DEFAULT 'month',
  `currency` enum('GBP','USD') NOT NULL DEFAULT 'GBP',
  `cache_date` datetime NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Plans cached from chargebee API';

CREATE TABLE `trivia_access` (
  `id` bigint(20) UNSIGNED NOT NULL COMMENT 'PK',
  `api_key` varchar(256) CHARACTER SET ascii DEFAULT NULL COMMENT 'API Key',
  `user_id` bigint(20) UNSIGNED NOT NULL COMMENT 'Discord ID of user',
  `enabled` tinyint(1) UNSIGNED NOT NULL DEFAULT 1
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

ALTER TABLE `bot_guild_settings`
  ADD PRIMARY KEY (`snowflake_id`),
  ADD UNIQUE KEY `custom_url` (`custom_url`),
  ADD KEY `premium` (`premium`),
  ADD KEY `only_mods_stop` (`only_mods_stop`),
  ADD KEY `role_reward_enabled` (`role_reward_enabled`),
  ADD KEY `role_reward_id` (`role_reward_id`);

ALTER TABLE `infobot_bandwidth`
  ADD PRIMARY KEY (`logdate`);

ALTER TABLE `infobot_discord_counts`
  ADD PRIMARY KEY (`shard_id`,`dev`),
  ADD KEY `last_updated` (`last_updated`);

ALTER TABLE `infobot_discord_list_sites`
  ADD PRIMARY KEY (`id`),
  ADD KEY `name` (`name`),
  ADD KEY `post_type` (`post_type`);

ALTER TABLE `infobot_shard_status`
  ADD PRIMARY KEY (`id`),
  ADD KEY `connected` (`connected`),
  ADD KEY `online` (`online`),
  ADD KEY `uptimerobot_heartbeat` (`uptimerobot_heartbeat`);

ALTER TABLE `infobot_votes`
  ADD PRIMARY KEY (`id`),
  ADD KEY `snowflake_id` (`snowflake_id`),
  ADD KEY `vote_time` (`vote_time`),
  ADD KEY `origin` (`origin`),
  ADD KEY `rolegiven` (`dm_hints`);

ALTER TABLE `infobot_vote_counters`
  ADD PRIMARY KEY (`snowflake_id`),
  ADD KEY `last_vote` (`last_vote`),
  ADD KEY `vote_count` (`vote_count`);

ALTER TABLE `infobot_vote_links`
  ADD PRIMARY KEY (`site`),
  ADD KEY `sortorder` (`sortorder`);

ALTER TABLE `premium_credits`
  ADD PRIMARY KEY (`user_id`,`subscription_id`),
  ADD UNIQUE KEY `guild_id` (`guild_id`),
  ADD KEY `active` (`active`),
  ADD KEY `since` (`since`),
  ADD KEY `fk_plan` (`plan_id`);

ALTER TABLE `premium_plans`
  ADD PRIMARY KEY (`id`),
  ADD KEY `lifetime` (`lifetime`),
  ADD KEY `period` (`period`),
  ADD KEY `period_unit` (`period_unit`),
  ADD KEY `cache_date` (`cache_date`);

ALTER TABLE `trivia_access`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `user_id` (`user_id`) USING BTREE,
  ADD UNIQUE KEY `key` (`api_key`) USING BTREE,
  ADD KEY `enabled` (`enabled`);

ALTER TABLE `infobot_discord_list_sites`
  MODIFY `id` bigint(20) UNSIGNED NOT NULL AUTO_INCREMENT;

ALTER TABLE `infobot_votes`
  MODIFY `id` bigint(20) UNSIGNED NOT NULL AUTO_INCREMENT;

ALTER TABLE `trivia_access`
  MODIFY `id` bigint(20) UNSIGNED NOT NULL AUTO_INCREMENT COMMENT 'PK';

ALTER TABLE `premium_credits`
  ADD CONSTRAINT `fk_plan` FOREIGN KEY (`plan_id`) REFERENCES `premium_plans` (`id`);
COMMIT;

