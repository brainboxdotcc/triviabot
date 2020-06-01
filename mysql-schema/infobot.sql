SET SQL_MODE = "NO_AUTO_VALUE_ON_ZERO";
SET time_zone = "+00:00";

CREATE TABLE `infobot` (
  `value` longtext DEFAULT NULL,
  `word` enum('is','can','are','has','cant','r','will','was','can''t','had','aren''t','might','may','arent') NOT NULL DEFAULT 'is',
  `setby` varchar(512) NOT NULL,
  `whenset` bigint(20) UNSIGNED NOT NULL,
  `locked` tinyint(1) UNSIGNED NOT NULL DEFAULT 0,
  `key_word` varchar(768) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE `infobot_cpu_graph` (
  `logdate` datetime NOT NULL DEFAULT current_timestamp(),
  `percent` decimal(7,4) UNSIGNED NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='stores details of cpu use for the bot';

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

CREATE TABLE `infobot_discord_javascript` (
  `id` bigint(20) UNSIGNED NOT NULL COMMENT 'Channel ID',
  `created` timestamp NOT NULL DEFAULT current_timestamp() COMMENT 'Creation date',
  `updated` timestamp NOT NULL DEFAULT current_timestamp() ON UPDATE current_timestamp() COMMENT 'Last modified date',
  `last_exec_ms` float NOT NULL DEFAULT 0 COMMENT 'Last execution time',
  `last_compile_ms` float NOT NULL DEFAULT 0 COMMENT 'Last compile time in ms',
  `script` longtext CHARACTER SET utf8mb4 DEFAULT NULL COMMENT 'Actual javascript content',
  `last_error` text CHARACTER SET utf8mb4 DEFAULT NULL COMMENT 'Last error message or empty/null',
  `last_memory_max` int(11) NOT NULL DEFAULT 0 COMMENT 'Last memory usage of script executed',
  `dirty` tinyint(1) UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Set to 1 if the bot is to reload this script'
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COMMENT='Information on which channels are using javascript replies';

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

CREATE TABLE `infobot_discord_settings` (
  `id` bigint(20) UNSIGNED NOT NULL COMMENT 'discord channel id',
  `parent_id` bigint(20) UNSIGNED DEFAULT NULL COMMENT 'Parent ID',
  `guild_id` bigint(20) UNSIGNED NOT NULL COMMENT 'Guild ID',
  `name` text CHARACTER SET utf8mb4 DEFAULT NULL COMMENT 'Channel name',
  `settings` longtext CHARACTER SET utf8mb4 NOT NULL,
  `tombstone` tinyint(1) UNSIGNED NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COMMENT='infobot settings for discord servers';

CREATE TABLE `infobot_discord_user_cache` (
  `id` bigint(20) UNSIGNED NOT NULL COMMENT 'Discord user ID',
  `username` varchar(750) NOT NULL COMMENT 'Username',
  `discriminator` char(4) CHARACTER SET latin1 COLLATE latin1_general_ci NOT NULL,
  `avatar` varchar(256) CHARACTER SET latin1 COLLATE latin1_general_ci DEFAULT NULL,
  `bot` tinyint(1) UNSIGNED NOT NULL DEFAULT 0 COMMENT 'User is a bot',
  `modified` timestamp NOT NULL DEFAULT current_timestamp() ON UPDATE current_timestamp() COMMENT 'Last modified timestamp'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='A database backed cache of discord users';

CREATE TABLE `infobot_javascript_kv` (
  `guild_id` bigint(20) UNSIGNED NOT NULL COMMENT 'Guild ID',
  `keyname` varchar(100) NOT NULL COMMENT 'Key name',
  `value` text NOT NULL COMMENT 'Value content'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Key/Value store for javascript users';
DELIMITER $$
CREATE TRIGGER `insert_check_max` BEFORE INSERT ON `infobot_javascript_kv` FOR EACH ROW CALL check_max(new.guild_id)
$$
DELIMITER ;
DELIMITER $$
CREATE TRIGGER `update_check_max` BEFORE UPDATE ON `infobot_javascript_kv` FOR EACH ROW CALL check_max(new.guild_id)
$$
DELIMITER ;

CREATE TABLE `infobot_membership` (
  `member_id` bigint(20) UNSIGNED NOT NULL,
  `guild_id` bigint(20) UNSIGNED NOT NULL,
  `nick` varchar(512) NOT NULL,
  `roles` varchar(4096) NOT NULL,
  `dashboard` tinyint(1) NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Guild memberships';

CREATE TABLE `infobot_shard_map` (
  `guild_id` bigint(20) UNSIGNED NOT NULL,
  `shard_id` int(10) UNSIGNED NOT NULL,
  `name` varchar(512) NOT NULL,
  `icon` varchar(200) NOT NULL,
  `unavailable` tinyint(1) UNSIGNED NOT NULL DEFAULT 0,
  `owner_id` bigint(20) UNSIGNED DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

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
  `rolegiven` tinyint(1) UNSIGNED NOT NULL DEFAULT 0
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

CREATE TABLE `infobot_web_requests` (
  `channel_id` bigint(20) UNSIGNED NOT NULL,
  `guild_id` bigint(20) UNSIGNED NOT NULL,
  `url` varchar(500) NOT NULL,
  `type` varchar(20) NOT NULL DEFAULT 'GET',
  `postdata` text DEFAULT NULL,
  `callback` varchar(500) NOT NULL,
  `returndata` longtext DEFAULT NULL,
  `statuscode` char(3) NOT NULL DEFAULT '000'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='List of queued web requests';

CREATE VIEW `vw_guild_members`  AS  select `infobot_discord_user_cache`.`id` AS `id`,`infobot_discord_user_cache`.`username` AS `username`,`infobot_discord_user_cache`.`discriminator` AS `discriminator`,`infobot_discord_user_cache`.`avatar` AS `avatar`,`infobot_discord_user_cache`.`bot` AS `bot`,`infobot_discord_user_cache`.`modified` AS `modified`,`infobot_shard_map`.`guild_id` AS `guild_id`,`infobot_shard_map`.`shard_id` AS `shard_id`,`infobot_shard_map`.`name` AS `name`,`infobot_shard_map`.`icon` AS `icon`,`infobot_shard_map`.`unavailable` AS `unavailable`,`infobot_shard_map`.`owner_id` AS `owner_id`,`infobot_membership`.`nick` AS `nick`,`infobot_membership`.`dashboard` AS `dashboard`,`infobot_membership`.`roles` AS `roles` from ((`infobot_membership` join `infobot_discord_user_cache` on(`infobot_discord_user_cache`.`id` = `infobot_membership`.`member_id`)) join `infobot_shard_map` on(`infobot_shard_map`.`guild_id` = `infobot_membership`.`guild_id`)) ;
DROP TABLE IF EXISTS `vw_infobot_active_voters`;

CREATE VIEW `vw_infobot_active_voters`  AS  select `infobot_votes`.`snowflake_id` AS `snowflake_id`,`infobot_discord_user_cache`.`username` AS `username`,`infobot_discord_user_cache`.`discriminator` AS `discriminator`,`infobot_votes`.`origin` AS `origin`,`infobot_votes`.`vote_time` AS `vote_time`,coalesce((select `infobot_vote_counters`.`vote_count` from `infobot_vote_counters` where `infobot_votes`.`snowflake_id` = `infobot_vote_counters`.`snowflake_id`),1) AS `vote_count` from (`infobot_discord_user_cache` left join `infobot_votes` on(`infobot_votes`.`snowflake_id` = `infobot_discord_user_cache`.`id`)) where `infobot_votes`.`vote_time` >= current_timestamp() - interval 1 day order by `infobot_votes`.`id` desc ;
DROP TABLE IF EXISTS `vw_infobot_vote_counts`;

CREATE VIEW `vw_infobot_vote_counts`  AS  select `infobot_vote_counters`.`snowflake_id` AS `snowflake_id`,coalesce(concat(`infobot_discord_user_cache`.`username`,'#',`infobot_discord_user_cache`.`discriminator`),'<unknown>') AS `user`,`infobot_vote_counters`.`vote_count` AS `vote_count`,`infobot_vote_counters`.`last_vote` AS `last_vote` from (`infobot_vote_counters` left join `infobot_discord_user_cache` on(`infobot_vote_counters`.`snowflake_id` = `infobot_discord_user_cache`.`id`)) ;


ALTER TABLE `infobot`
  ADD PRIMARY KEY (`key_word`),
  ADD KEY `word_idx` (`word`),
  ADD KEY `locked_idx` (`locked`),
  ADD KEY `setby_idx` (`setby`),
  ADD KEY `whenset` (`whenset`);

ALTER TABLE `infobot_cpu_graph`
  ADD PRIMARY KEY (`logdate`);

ALTER TABLE `infobot_discord_counts`
  ADD PRIMARY KEY (`shard_id`,`dev`),
  ADD KEY `last_updated` (`last_updated`);

ALTER TABLE `infobot_discord_javascript`
  ADD PRIMARY KEY (`id`),
  ADD KEY `dirty` (`dirty`),
  ADD KEY `created` (`created`),
  ADD KEY `updated` (`updated`);

ALTER TABLE `infobot_discord_list_sites`
  ADD PRIMARY KEY (`id`),
  ADD KEY `name` (`name`),
  ADD KEY `post_type` (`post_type`);

ALTER TABLE `infobot_discord_settings`
  ADD PRIMARY KEY (`id`),
  ADD KEY `guild_id` (`guild_id`),
  ADD KEY `parent_id` (`parent_id`);

ALTER TABLE `infobot_discord_user_cache`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `id` (`id`),
  ADD KEY `discriminator` (`discriminator`),
  ADD KEY `username` (`username`),
  ADD KEY `bot` (`bot`),
  ADD KEY `modified` (`modified`);

ALTER TABLE `infobot_javascript_kv`
  ADD PRIMARY KEY (`guild_id`,`keyname`);

ALTER TABLE `infobot_membership`
  ADD PRIMARY KEY (`member_id`,`guild_id`),
  ADD KEY `member_id` (`member_id`),
  ADD KEY `guild_id` (`guild_id`),
  ADD KEY `dashboard` (`dashboard`);

ALTER TABLE `infobot_shard_map`
  ADD PRIMARY KEY (`guild_id`),
  ADD KEY `owner_id` (`owner_id`);

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
  ADD KEY `rolegiven` (`rolegiven`);

ALTER TABLE `infobot_vote_counters`
  ADD PRIMARY KEY (`snowflake_id`),
  ADD KEY `last_vote` (`last_vote`),
  ADD KEY `vote_count` (`vote_count`);

ALTER TABLE `infobot_vote_links`
  ADD PRIMARY KEY (`site`),
  ADD KEY `sortorder` (`sortorder`);


ALTER TABLE `infobot_discord_list_sites`
  MODIFY `id` bigint(20) UNSIGNED NOT NULL AUTO_INCREMENT;

ALTER TABLE `infobot_votes`
  MODIFY `id` bigint(20) UNSIGNED NOT NULL AUTO_INCREMENT;

