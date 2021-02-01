SET FOREIGN_KEY_CHECKS=0;
SET SQL_MODE = "NO_AUTO_VALUE_ON_ZERO";
SET AUTOCOMMIT = 0;
START TRANSACTION;
SET time_zone = "+00:00";

/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;
/*!40101 SET NAMES utf8mb4 */;

CREATE DATABASE IF NOT EXISTS `triviabot` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;
USE `triviabot`;

DELIMITER $$
DROP PROCEDURE IF EXISTS `add_question`$$
CREATE DEFINER=`admin`@`localhost` PROCEDURE `add_question` (IN `_question` VARCHAR(255) CHARSET utf8mb4, IN `_answer` VARCHAR(255) CHARSET utf8mb4, IN `_category_id` INT(20) UNSIGNED, IN `_hint1` VARCHAR(255) CHARSET utf8mb4, IN `_hint2` VARCHAR(255) CHARSET utf8mb4, IN `_question_img` VARCHAR(255) CHARSET latin1, IN `_answer_img` VARCHAR(255) CHARSET latin1)  MODIFIES SQL DATA
    SQL SECURITY INVOKER
BEGIN
START TRANSACTION;
SET @qid = (SELECT qcount FROM counters LIMIT 1);
INSERT INTO questions (id, question, category, question_img_url) VALUES(@qid, _question, _category_id, _question_img);
INSERT INTO answers (id, answer, answer_img_url) VALUES(@qid, _answer, _answer_img);
INSERT INTO hints (id, hint1, hint2) VALUES(@qid, _hint1, _hint2);
INSERT INTO stats (id, timesasked, record_time) VALUES(@qid, 0, 99999);
UPDATE counters SET qcount = qcount + 1;
COMMIT;
END$$

DROP FUNCTION IF EXISTS `count_remaining`$$
CREATE DEFINER=`admin`@`localhost` FUNCTION `count_remaining` (`_guild_id` BIGINT(20) UNSIGNED) RETURNS BIGINT(20) UNSIGNED READS SQL DATA
    SQL SECURITY INVOKER
    COMMENT 'Counts remaining enabled questions'
BEGIN
RETURN  
(SELECT COUNT(*) FROM questions INNER JOIN categories ON categories.id = questions.category WHERE disabled != 1)
-
(SELECT SUM(IF(guild_id IS NULL, 0, (SELECT COUNT(*) FROM questions WHERE category = categories.id))) AS total_disabled FROM categories LEFT JOIN disabled_categories ON categories.id = disabled_categories.category_id AND guild_id = _guild_id WHERE disabled != 1);
END$$

DROP FUNCTION IF EXISTS `find_list_in_list`$$
CREATE DEFINER=`admin`@`localhost` FUNCTION `find_list_in_list` (`list1` VARCHAR(8192) CHARSET ascii, `list2` VARCHAR(8192) CHARSET ascii) RETURNS TINYINT(1) UNSIGNED NO SQL
    DETERMINISTIC
    SQL SECURITY INVOKER
BEGIN
DECLARE _list1 VARCHAR(8192);
DECLARE _list2 VARCHAR(8192);
IF (list1 = '' OR list2 = '') THEN 
    RETURN 0;
END IF;
SET @_list1 = REPLACE(TRIM(list1), ' ', '| ');
SET @_list2 = REPLACE(TRIM(list2), ' ', '| ');
IF (@_list1 != '' AND list2 RLIKE @_list1) THEN
    return 1;
END IF;
IF (@_list2 != '' AND list1 RLIKE @_list2) THEN
    return 1;
END IF;
return 0;
END$$

DROP FUNCTION IF EXISTS `is_admin`$$
CREATE DEFINER=`admin`@`localhost` FUNCTION `is_admin` (`_user_id` BIGINT(20) UNSIGNED, `_guild_id` BIGINT(20) UNSIGNED) RETURNS TINYINT(1) UNSIGNED BEGIN
declare admin_roles int;
declare owner int;

set @owner = (select count(snowflake_id) from trivia_guild_cache where snowflake_id = _guild_id AND owner_id = _user_id);

IF @owner > 0 THEN
RETURN @owner;
END IF;

set @admin_roles = (select count(id) from trivia_role_cache where guild_id = _guild_id AND id RLIKE (SELECT group_concat(ltrim(rtrim(replace(roles,' ','|'))) separator '|') FROM `trivia_guild_membership` where user_id = _user_id and guild_id = _guild_id) AND (permissions & 0x08) = 0x08);

RETURN @admin_roles;
END$$

DROP FUNCTION IF EXISTS `percent_disabled`$$
CREATE DEFINER=`admin`@`localhost` FUNCTION `percent_disabled` (`_guild_id` BIGINT(20) UNSIGNED) RETURNS FLOAT UNSIGNED READS SQL DATA
    SQL SECURITY INVOKER
    COMMENT 'Returns the percentage of questions a guild has disabled'
BEGIN
RETURN  
(SELECT SUM(IF(guild_id IS NULL, 0, (SELECT COUNT(*) FROM questions WHERE category = categories.id))) AS total_disabled FROM categories LEFT JOIN disabled_categories ON categories.id = disabled_categories.category_id AND guild_id = _guild_id WHERE disabled != 1 ORDER BY `categories`.`name`) / 
(SELECT COUNT(*) FROM questions INNER JOIN categories ON categories.id = questions.category WHERE disabled != 1)
* 100;
END$$

DROP FUNCTION IF EXISTS `random_string`$$
CREATE DEFINER=`admin`@`localhost` FUNCTION `random_string` (`length` SMALLINT(3), `seed` VARCHAR(255)) RETURNS VARCHAR(255) CHARSET utf8 NO SQL
BEGIN
    SET @output = '';

    IF seed IS NULL OR seed = '' THEN SET seed = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789'; END IF;

    SET @rnd_multiplier = LENGTH(seed);

    WHILE LENGTH(@output) < length DO
        # Select random character and add to output
        SET @output = CONCAT(@output, SUBSTRING(seed, RAND() * (@rnd_multiplier + 1), 1));
    END WHILE;

    RETURN @output;
END$$

DROP FUNCTION IF EXISTS `replace_ci`$$
CREATE DEFINER=`admin`@`localhost` FUNCTION `replace_ci` (`str` TEXT, `needle` CHAR(255), `str_rep` CHAR(255)) RETURNS TEXT CHARSET utf8mb4 BEGIN
DECLARE return_str TEXT DEFAULT '';
DECLARE lower_str TEXT;
DECLARE lower_needle TEXT;
DECLARE pos INT DEFAULT 1;
DECLARE old_pos INT DEFAULT 1;

IF needle = '' THEN
RETURN str;
END IF;

SELECT lower(str) INTO lower_str;
SELECT lower(needle) INTO lower_needle;
SELECT locate(lower_needle, lower_str, pos) INTO pos;
WHILE pos > 0 DO
SELECT concat(return_str, substr(str, old_pos, pos-old_pos), str_rep) INTO return_str;
SELECT pos + char_length(needle) INTO pos;
SELECT pos INTO old_pos;
SELECT locate(lower_needle, lower_str, pos) INTO pos;
END WHILE;
SELECT concat(return_str, substr(str, old_pos, char_length(str))) INTO return_str;
RETURN return_str;
END$$

DELIMITER ;

DROP TABLE IF EXISTS `achievements`;
CREATE TABLE `achievements` (
  `user_id` bigint(20) UNSIGNED NOT NULL,
  `achievement_id` int(10) UNSIGNED NOT NULL,
  `unlocked` datetime NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `active_games`;
CREATE TABLE `active_games` (
  `guild_id` bigint(20) UNSIGNED NOT NULL,
  `channel_id` bigint(20) UNSIGNED NOT NULL,
  `hostname` varchar(128) NOT NULL,
  `cluster_id` bigint(20) UNSIGNED DEFAULT 0,
  `questions` int(10) UNSIGNED NOT NULL,
  `quickfire` tinyint(1) UNSIGNED NOT NULL DEFAULT 0,
  `question_index` int(10) UNSIGNED DEFAULT 1,
  `started` datetime DEFAULT current_timestamp(),
  `channel_name` varchar(512) DEFAULT NULL,
  `user_id` bigint(20) UNSIGNED NOT NULL,
  `streak` int(10) UNSIGNED DEFAULT 1,
  `lastanswered` bigint(20) UNSIGNED DEFAULT 0,
  `state` tinyint(1) DEFAULT 1,
  `hintless` tinyint(1) UNSIGNED DEFAULT 0,
  `qlist` longtext DEFAULT NULL,
  `stop` tinyint(1) UNSIGNED NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `active_roles_rewarded`;
CREATE TABLE `active_roles_rewarded` (
  `guild_id` bigint(20) UNSIGNED NOT NULL,
  `user_id` bigint(20) UNSIGNED NOT NULL,
  `role_id` bigint(20) UNSIGNED NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `answers`;
CREATE TABLE `answers` (
  `id` bigint(20) UNSIGNED NOT NULL,
  `answer` longtext DEFAULT NULL,
  `answer_img_url` varchar(512) CHARACTER SET latin1 COLLATE latin1_general_ci DEFAULT NULL COMMENT 'Image to show in answer embed, if non-null',
  `trans_tr` text DEFAULT NULL,
  `trans_pt` text DEFAULT NULL,
  `trans_fr` text DEFAULT NULL,
  `trans_es` text DEFAULT NULL,
  `trans_de` text DEFAULT NULL,
  `trans_hi` text DEFAULT NULL,
  `trans_sv` text DEFAULT NULL,
  `trans_ru` text DEFAULT NULL,
  `trans_pl` text DEFAULT NULL,
  `trans_it` text DEFAULT NULL,
  `trans_ja` text DEFAULT NULL,
  `trans_ko` text DEFAULT NULL,
  `trans_bg` text DEFAULT NULL,
  `trans_zh` text DEFAULT NULL,
  `trans_ar` text DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `bot_guild_settings`;
CREATE TABLE `bot_guild_settings` (
  `snowflake_id` bigint(20) UNSIGNED NOT NULL COMMENT 'PK',
  `prefix` varchar(64) NOT NULL DEFAULT '!' COMMENT 'Server''s prefix',
  `embedcolour` int(10) UNSIGNED NOT NULL DEFAULT 3238819 COMMENT 'Colour used in embeds',
  `moderator_roles` text DEFAULT NULL COMMENT 'Space seperated list of moderator role IDs',
  `premium` tinyint(1) UNSIGNED NOT NULL DEFAULT 0 COMMENT 'True if premium guild',
  `only_mods_stop` tinyint(1) UNSIGNED NOT NULL DEFAULT 0 COMMENT 'True if only moderators can do !trivia stop',
  `only_mods_start` tinyint(1) UNSIGNED DEFAULT 0,
  `role_reward_enabled` tinyint(1) UNSIGNED NOT NULL DEFAULT 0 COMMENT 'True if role rewards are enabled',
  `role_reward_id` bigint(20) UNSIGNED DEFAULT NULL COMMENT 'Role snowflake ID',
  `custom_css` longtext DEFAULT NULL COMMENT 'Custom Stats CSS for premium users only',
  `custom_url` varchar(250) DEFAULT NULL,
  `points_reward_threshold` int(10) UNSIGNED DEFAULT NULL,
  `points_reward_role_id` bigint(20) UNSIGNED DEFAULT NULL,
  `language` varchar(20) DEFAULT 'en',
  `question_interval` int(8) UNSIGNED DEFAULT 20 COMMENT 'Amount of time between questions in non-quickfire rounds'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Stores guild specific settings';

DROP TABLE IF EXISTS `categories`;
CREATE TABLE `categories` (
  `id` bigint(20) UNSIGNED NOT NULL,
  `name` varchar(256) NOT NULL DEFAULT 'unknown',
  `disabled` tinyint(1) UNSIGNED NOT NULL DEFAULT 0,
  `weight` decimal(3,2) UNSIGNED NOT NULL DEFAULT 1.00,
  `selection_count` bigint(20) UNSIGNED DEFAULT 0 COMMENT 'number of times this category has been chosen to be played on its own (regardless of if it was allowed)',
  `questions_asked` bigint(20) UNSIGNED DEFAULT 0,
  `translation_dirty` tinyint(1) UNSIGNED DEFAULT 0,
  `trans_tr` text DEFAULT NULL,
  `trans_pt` text DEFAULT NULL,
  `trans_fr` text DEFAULT NULL,
  `trans_es` text DEFAULT NULL,
  `trans_de` text DEFAULT NULL,
  `trans_hi` text DEFAULT NULL,
  `trans_sv` text DEFAULT NULL,
  `trans_ru` text DEFAULT NULL,
  `trans_pl` text DEFAULT NULL,
  `trans_it` text DEFAULT NULL,
  `trans_ja` text DEFAULT NULL,
  `trans_ko` text DEFAULT NULL,
  `trans_bg` text DEFAULT NULL,
  `trans_zh` text DEFAULT NULL,
  `trans_ar` text DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `counters`;
CREATE TABLE `counters` (
  `qcount` bigint(20) DEFAULT NULL,
  `asked` bigint(20) UNSIGNED NOT NULL DEFAULT 0,
  `asked_15_min` bigint(20) UNSIGNED DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `disabled_categories`;
CREATE TABLE `disabled_categories` (
  `guild_id` bigint(20) UNSIGNED NOT NULL,
  `category_id` bigint(20) UNSIGNED NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `dm_queue`;
CREATE TABLE `dm_queue` (
  `id` bigint(20) UNSIGNED NOT NULL,
  `snowflake_id` bigint(20) UNSIGNED NOT NULL,
  `json_message` longtext NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `feedback`;
CREATE TABLE `feedback` (
  `id` bigint(20) UNSIGNED NOT NULL COMMENT 'primary key',
  `question_id` bigint(20) UNSIGNED DEFAULT NULL COMMENT 'normal question id',
  `insane_id` bigint(20) UNSIGNED DEFAULT NULL COMMENT 'insane question id',
  `user_id` bigint(20) UNSIGNED NOT NULL COMMENT 'user who submitted feedback',
  `feedback` text NOT NULL COMMENT 'feedback from user',
  `closed_by` bigint(20) UNSIGNED DEFAULT NULL COMMENT 'Mod who closed the feedback or null if its still open',
  `submitted` datetime DEFAULT NULL COMMENT 'time/date submitted',
  `mod_notes` text DEFAULT NULL COMMENT 'moderator notes'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `game_score_history`;
CREATE TABLE `game_score_history` (
  `url_key` varchar(10) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `guild_id` bigint(20) UNSIGNED NOT NULL,
  `timestarted` datetime NOT NULL,
  `timefinished` datetime NOT NULL,
  `scores` text NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
DROP TRIGGER IF EXISTS `set_url_key`;
DELIMITER $$
CREATE TRIGGER `set_url_key` BEFORE INSERT ON `game_score_history` FOR EACH ROW SET new.url_key = random_string(10, '')
$$
DELIMITER ;

DROP TABLE IF EXISTS `hints`;
CREATE TABLE `hints` (
  `id` bigint(20) UNSIGNED NOT NULL,
  `hint1` longtext DEFAULT NULL,
  `hint2` longtext DEFAULT NULL,
  `trans1_tr` text DEFAULT NULL,
  `trans1_pt` text DEFAULT NULL,
  `trans1_fr` text DEFAULT NULL,
  `trans2_tr` text DEFAULT NULL,
  `trans2_pt` text DEFAULT NULL,
  `trans2_fr` text DEFAULT NULL,
  `trans1_es` text DEFAULT NULL,
  `trans2_es` text DEFAULT NULL,
  `trans1_de` text DEFAULT NULL,
  `trans2_de` text DEFAULT NULL,
  `trans1_hi` text DEFAULT NULL,
  `trans2_hi` text DEFAULT NULL,
  `trans1_sv` text DEFAULT NULL,
  `trans2_sv` text DEFAULT NULL,
  `trans1_ru` text DEFAULT NULL,
  `trans2_ru` text DEFAULT NULL,
  `trans1_pl` text DEFAULT NULL,
  `trans2_pl` text DEFAULT NULL,
  `trans1_it` text DEFAULT NULL,
  `trans2_it` text DEFAULT NULL,
  `trans1_ja` text DEFAULT NULL,
  `trans2_ja` text DEFAULT NULL,
  `trans1_ko` text DEFAULT NULL,
  `trans2_ko` text DEFAULT NULL,
  `trans1_bg` text DEFAULT NULL,
  `trans2_bg` text DEFAULT NULL,
  `trans1_zh` text DEFAULT NULL,
  `trans2_zh` text DEFAULT NULL,
  `trans1_ar` text DEFAULT NULL,
  `trans2_ar` text DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `infobot_bandwidth`;
CREATE TABLE `infobot_bandwidth` (
  `logdate` timestamp NOT NULL DEFAULT current_timestamp(),
  `kbps_in` double NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `infobot_discord_counts`;
CREATE TABLE `infobot_discord_counts` (
  `shard_id` bigint(20) UNSIGNED NOT NULL COMMENT 'Shard ID',
  `cluster_id` int(10) UNSIGNED NOT NULL DEFAULT 0,
  `dev` tinyint(1) UNSIGNED NOT NULL DEFAULT 0 COMMENT 'true if development data',
  `user_count` bigint(20) NOT NULL,
  `server_count` bigint(20) NOT NULL,
  `shard_count` bigint(20) UNSIGNED NOT NULL DEFAULT 1 COMMENT 'number of shards',
  `channel_count` bigint(20) UNSIGNED NOT NULL,
  `sent_messages` bigint(20) UNSIGNED NOT NULL DEFAULT 0,
  `received_messages` bigint(20) UNSIGNED NOT NULL DEFAULT 0,
  `memory_usage` bigint(20) UNSIGNED NOT NULL DEFAULT 0,
  `games` bigint(20) NOT NULL DEFAULT 0 COMMENT 'Number of concurrent active games in progress',
  `last_updated` datetime NOT NULL DEFAULT current_timestamp() ON UPDATE current_timestamp(),
  `last_restart_intervention` datetime DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COMMENT='Counts of users/servers on a per-shard basis';

DROP TABLE IF EXISTS `infobot_discord_list_sites`;
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

DROP TABLE IF EXISTS `infobot_shard_status`;
CREATE TABLE `infobot_shard_status` (
  `id` int(10) UNSIGNED NOT NULL,
  `cluster_id` int(10) UNSIGNED DEFAULT 0,
  `connected` tinyint(1) UNSIGNED NOT NULL,
  `online` tinyint(1) UNSIGNED NOT NULL,
  `uptime` bigint(20) UNSIGNED NOT NULL,
  `transfer` bigint(20) UNSIGNED NOT NULL,
  `transfer_compressed` bigint(20) UNSIGNED NOT NULL,
  `uptimerobot_heartbeat` varchar(300) DEFAULT NULL,
  `updated` timestamp NULL DEFAULT current_timestamp() ON UPDATE current_timestamp(),
  `uptimerobot_response` varchar(255) DEFAULT NULL,
  `forcereconnect` tinyint(1) UNSIGNED DEFAULT 0,
  `uptimerobot_id` bigint(20) UNSIGNED DEFAULT NULL,
  `reconnect_status` varchar(200) DEFAULT NULL,
  `last_reconnect_intervention` datetime DEFAULT NULL,
  `down_since` datetime DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Contains details of all active shards';

DROP TABLE IF EXISTS `infobot_votes`;
CREATE TABLE `infobot_votes` (
  `id` bigint(20) UNSIGNED NOT NULL,
  `snowflake_id` bigint(20) UNSIGNED NOT NULL,
  `vote_time` timestamp NULL DEFAULT current_timestamp(),
  `origin` varchar(80) NOT NULL,
  `dm_hints` tinyint(2) UNSIGNED NOT NULL DEFAULT 8
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `infobot_vote_counters`;
CREATE TABLE `infobot_vote_counters` (
  `snowflake_id` bigint(20) UNSIGNED NOT NULL,
  `vote_count` bigint(20) UNSIGNED NOT NULL DEFAULT 0,
  `last_vote` datetime NOT NULL DEFAULT current_timestamp() ON UPDATE current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Counters of votes cast';

DROP TABLE IF EXISTS `infobot_vote_links`;
CREATE TABLE `infobot_vote_links` (
  `site` varchar(80) NOT NULL,
  `vote_url` varchar(256) NOT NULL,
  `sortorder` float NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Voting URLs for the sites which have webhooks';

DROP TABLE IF EXISTS `insane`;
CREATE TABLE `insane` (
  `id` bigint(20) UNSIGNED NOT NULL,
  `question` text DEFAULT NULL,
  `deleted` tinyint(1) UNSIGNED DEFAULT NULL,
  `translation_dirty` tinyint(1) UNSIGNED NOT NULL DEFAULT 0,
  `trans_tr` text DEFAULT NULL,
  `trans_pt` text DEFAULT NULL,
  `trans_fr` text DEFAULT NULL,
  `trans_es` text DEFAULT NULL,
  `trans_de` text DEFAULT NULL,
  `trans_hi` text DEFAULT NULL,
  `trans_sv` text DEFAULT NULL,
  `trans_ru` text DEFAULT NULL,
  `trans_pl` text DEFAULT NULL,
  `trans_it` text DEFAULT NULL,
  `trans_ja` text DEFAULT NULL,
  `trans_ko` text DEFAULT NULL,
  `trans_bg` text DEFAULT NULL,
  `trans_zh` text DEFAULT NULL,
  `trans_ar` text DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `insane_answers`;
CREATE TABLE `insane_answers` (
  `id` bigint(20) UNSIGNED NOT NULL,
  `question_id` bigint(20) UNSIGNED DEFAULT NULL,
  `answer` text DEFAULT NULL,
  `translation_dirty` tinyint(1) UNSIGNED NOT NULL DEFAULT 0,
  `trans_tr` text DEFAULT NULL,
  `trans_pt` text DEFAULT NULL,
  `trans_fr` text DEFAULT NULL,
  `trans_es` text DEFAULT NULL,
  `trans_de` text DEFAULT NULL,
  `trans_hi` text DEFAULT NULL,
  `trans_sv` text DEFAULT NULL,
  `trans_ru` text DEFAULT NULL,
  `trans_pl` text DEFAULT NULL,
  `trans_it` text DEFAULT NULL,
  `trans_ja` text DEFAULT NULL,
  `trans_ko` text DEFAULT NULL,
  `trans_bg` text DEFAULT NULL,
  `trans_zh` text DEFAULT NULL,
  `trans_ar` text DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `insane_round_statistics`;
CREATE TABLE `insane_round_statistics` (
  `guild_id` bigint(20) UNSIGNED NOT NULL,
  `channel_id` bigint(20) UNSIGNED NOT NULL,
  `user_id` bigint(20) UNSIGNED NOT NULL,
  `score` int(10) UNSIGNED NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `languages`;
CREATE TABLE `languages` (
  `id` bigint(20) NOT NULL,
  `isocode` char(2) CHARACTER SET ascii NOT NULL,
  `flag` text DEFAULT NULL,
  `name` varchar(255) NOT NULL,
  `live` tinyint(1) UNSIGNED NOT NULL DEFAULT 0,
  `altcode` varchar(8) DEFAULT NULL,
  `emoji` varchar(30) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `new_daywinners`;
CREATE TABLE `new_daywinners` (
  `guild_id` bigint(20) UNSIGNED NOT NULL,
  `datestamp` date NOT NULL,
  `user_id` bigint(20) UNSIGNED DEFAULT NULL,
  `score` int(10) UNSIGNED DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `numstrs`;
CREATE TABLE `numstrs` (
  `id` bigint(20) UNSIGNED NOT NULL,
  `value` bigint(20) UNSIGNED NOT NULL,
  `description` varchar(255) NOT NULL,
  `translation_dirty` tinyint(1) UNSIGNED DEFAULT 0,
  `trans_tr` varchar(255) DEFAULT NULL,
  `trans_pt` varchar(255) DEFAULT NULL,
  `trans_fr` varchar(255) DEFAULT NULL,
  `trans_es` varchar(255) DEFAULT NULL,
  `trans_de` varchar(255) DEFAULT NULL,
  `trans_hi` varchar(255) DEFAULT NULL,
  `trans_sv` text DEFAULT NULL,
  `trans_ru` text DEFAULT NULL,
  `trans_pl` text DEFAULT NULL,
  `trans_it` text DEFAULT NULL,
  `trans_ja` text DEFAULT NULL,
  `trans_ko` text DEFAULT NULL,
  `trans_bg` text DEFAULT NULL,
  `trans_zh` text DEFAULT NULL,
  `trans_ar` text DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `premium_credits`;
CREATE TABLE `premium_credits` (
  `user_id` bigint(20) UNSIGNED NOT NULL COMMENT 'Discord snowflake ID',
  `subscription_id` varchar(255) CHARACTER SET ascii NOT NULL COMMENT 'Subscription ID from chargebee',
  `guild_id` bigint(20) UNSIGNED DEFAULT NULL COMMENT 'Guild ID subscription is attached to, if any',
  `active` tinyint(1) UNSIGNED NOT NULL DEFAULT 1 COMMENT 'True if the subscription is currently active',
  `since` datetime NOT NULL DEFAULT current_timestamp() COMMENT 'Date of subscription start',
  `plan_id` varchar(250) CHARACTER SET ascii NOT NULL COMMENT 'Plan associated with the subscription',
  `cancel_date` datetime DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `premium_plans`;
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

DROP TABLE IF EXISTS `questions`;
CREATE TABLE `questions` (
  `id` bigint(20) UNSIGNED NOT NULL DEFAULT 0,
  `category` bigint(20) UNSIGNED NOT NULL,
  `question` varchar(512) NOT NULL DEFAULT '',
  `submitted_by` bigint(20) UNSIGNED DEFAULT NULL,
  `no_auto_translate` tinyint(1) UNSIGNED NOT NULL DEFAULT 0,
  `translation_dirty` tinyint(1) UNSIGNED NOT NULL DEFAULT 0 COMMENT 'If set to 1, translation of this question, answer and hints must be re-run',
  `approved_by` bigint(20) UNSIGNED DEFAULT NULL,
  `last_edited_by` bigint(20) UNSIGNED DEFAULT NULL,
  `approved_date` datetime DEFAULT NULL,
  `last_edited_date` datetime DEFAULT NULL,
  `question_img_url` varchar(512) CHARACTER SET latin1 COLLATE latin1_general_ci DEFAULT NULL COMMENT 'image url to use in embed, if set to non-null',
  `trans_tr` text DEFAULT NULL,
  `trans_pt` text DEFAULT NULL,
  `trans_fr` text DEFAULT NULL,
  `trans_es` text DEFAULT NULL,
  `trans_de` text DEFAULT NULL,
  `trans_hi` text DEFAULT NULL,
  `trans_sv` text DEFAULT NULL,
  `trans_ru` text DEFAULT NULL,
  `trans_pl` text DEFAULT NULL,
  `trans_it` text DEFAULT NULL,
  `trans_ja` text DEFAULT NULL,
  `trans_ko` text DEFAULT NULL,
  `trans_bg` text DEFAULT NULL,
  `trans_zh` text DEFAULT NULL,
  `trans_ar` text DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `question_queue`;
CREATE TABLE `question_queue` (
  `id` bigint(20) NOT NULL,
  `snowflake_id` bigint(20) UNSIGNED NOT NULL COMMENT 'User who requested addition of the question',
  `category` bigint(20) NOT NULL,
  `question` varchar(512) NOT NULL,
  `answer` varchar(128) NOT NULL,
  `hint1` varchar(255) DEFAULT NULL,
  `hint2` varchar(255) DEFAULT NULL,
  `question_image` varchar(255) CHARACTER SET latin1 COLLATE latin1_general_ci DEFAULT NULL,
  `answer_image` varchar(255) CHARACTER SET latin1 COLLATE latin1_general_ci DEFAULT NULL,
  `dateadded` datetime DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `role_change_queue`;
CREATE TABLE `role_change_queue` (
  `id` bigint(20) UNSIGNED NOT NULL,
  `guild_id` bigint(20) UNSIGNED NOT NULL,
  `user_id` bigint(20) UNSIGNED NOT NULL,
  `role_id` bigint(20) UNSIGNED NOT NULL,
  `adding` tinyint(1) UNSIGNED NOT NULL DEFAULT 1
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `scheduled_games`;
CREATE TABLE `scheduled_games` (
  `id` bigint(20) UNSIGNED NOT NULL,
  `guild_id` bigint(20) UNSIGNED NOT NULL,
  `channel_id` bigint(20) UNSIGNED NOT NULL,
  `user_id` bigint(20) UNSIGNED NOT NULL,
  `quickfire` tinyint(1) UNSIGNED NOT NULL DEFAULT 0,
  `questions` int(11) NOT NULL,
  `start_time` time NOT NULL,
  `queuetime` datetime NOT NULL DEFAULT current_timestamp(),
  `hintless` tinyint(1) UNSIGNED DEFAULT 0,
  `category` varchar(250) DEFAULT NULL,
  `announce_mins` int(10) UNSIGNED DEFAULT NULL COMMENT 'Minutes before game to announce it',
  `announce_message` text DEFAULT NULL,
  `announce_ping` bigint(20) UNSIGNED DEFAULT NULL,
  `announce_time` time GENERATED ALWAYS AS (cast(from_unixtime(time_to_sec(`start_time`) + 82800 - 60 * `announce_mins`) as time)) VIRTUAL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `scores`;
CREATE TABLE `scores` (
  `name` bigint(20) UNSIGNED NOT NULL COMMENT 'PK snowflake ID',
  `guild_id` bigint(20) UNSIGNED NOT NULL COMMENT 'Guild id of score',
  `score` bigint(20) UNSIGNED NOT NULL DEFAULT 0,
  `dayscore` bigint(20) NOT NULL DEFAULT 0,
  `weekscore` bigint(20) UNSIGNED NOT NULL DEFAULT 0,
  `monthscore` bigint(20) UNSIGNED NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `scores_lastgame`;
CREATE TABLE `scores_lastgame` (
  `guild_id` bigint(20) UNSIGNED NOT NULL,
  `user_id` bigint(20) UNSIGNED NOT NULL,
  `score` int(10) UNSIGNED NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `start_log`;
CREATE TABLE `start_log` (
  `user_id` bigint(20) UNSIGNED NOT NULL,
  `last_started` datetime NOT NULL,
  `consecutive` int(10) UNSIGNED NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Stores the time and date and number of consecutive days a user has started trivia, so that we can check for achievements';

DROP TABLE IF EXISTS `start_queue`;
CREATE TABLE `start_queue` (
  `guild_id` bigint(20) UNSIGNED NOT NULL,
  `channel_id` bigint(20) UNSIGNED NOT NULL,
  `user_id` bigint(20) UNSIGNED NOT NULL,
  `quickfire` tinyint(1) UNSIGNED NOT NULL DEFAULT 0,
  `questions` int(11) NOT NULL,
  `queuetime` datetime NOT NULL DEFAULT current_timestamp(),
  `hintless` tinyint(1) UNSIGNED DEFAULT 0,
  `category` varchar(250) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `stats`;
CREATE TABLE `stats` (
  `id` bigint(20) UNSIGNED NOT NULL,
  `lastasked` bigint(20) DEFAULT 0,
  `timesasked` bigint(20) DEFAULT NULL,
  `lastcorrect` bigint(20) UNSIGNED DEFAULT NULL,
  `record_time` bigint(20) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `streaks`;
CREATE TABLE `streaks` (
  `nick` bigint(20) UNSIGNED NOT NULL,
  `guild_id` bigint(20) UNSIGNED NOT NULL,
  `streak` bigint(20) NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `teams`;
CREATE TABLE `teams` (
  `url_key` varchar(10) CHARACTER SET ascii COLLATE ascii_bin DEFAULT NULL,
  `name` varchar(128) NOT NULL DEFAULT '',
  `score` bigint(20) DEFAULT NULL,
  `owner_id` bigint(20) UNSIGNED DEFAULT NULL,
  `create_date` datetime DEFAULT current_timestamp(),
  `discord_invite` varchar(50) DEFAULT NULL,
  `web_address` varchar(250) DEFAULT NULL,
  `description` text DEFAULT NULL,
  `image_url` varchar(512) DEFAULT NULL,
  `show_member_list` tinyint(1) UNSIGNED NOT NULL DEFAULT 1,
  `custom_css` text DEFAULT NULL COMMENT 'Custom Stats CSS for premium users only',
  `team_url` varchar(128) DEFAULT NULL,
  `qualifying_score` int(10) UNSIGNED DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
DROP TRIGGER IF EXISTS `set_key`;
DELIMITER $$
CREATE TRIGGER `set_key` BEFORE INSERT ON `teams` FOR EACH ROW SET new.url_key = random_string(10, '')
$$
DELIMITER ;

DROP TABLE IF EXISTS `team_membership`;
CREATE TABLE `team_membership` (
  `nick` bigint(20) UNSIGNED NOT NULL,
  `team` varchar(128) NOT NULL,
  `joined` bigint(20) DEFAULT NULL,
  `points_contributed` bigint(20) UNSIGNED DEFAULT NULL,
  `privacy_mode` tinyint(1) UNSIGNED NOT NULL DEFAULT 0 COMMENT 'set to 1 to hide from team hub',
  `team_manager` tinyint(1) UNSIGNED NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `trivia_access`;
CREATE TABLE `trivia_access` (
  `id` bigint(20) UNSIGNED NOT NULL COMMENT 'PK',
  `api_key` varchar(256) CHARACTER SET ascii DEFAULT NULL COMMENT 'API Key',
  `user_id` bigint(20) UNSIGNED NOT NULL COMMENT 'Discord ID of user',
  `enabled` tinyint(1) UNSIGNED NOT NULL DEFAULT 1,
  `review_queue` tinyint(1) UNSIGNED NOT NULL DEFAULT 0,
  `restart_bot` tinyint(1) UNSIGNED NOT NULL DEFAULT 0,
  `game_control` tinyint(1) UNSIGNED NOT NULL DEFAULT 0,
  `delete_question` tinyint(1) UNSIGNED NOT NULL DEFAULT 0,
  `add_category` tinyint(1) UNSIGNED NOT NULL DEFAULT 0,
  `add_question` tinyint(1) UNSIGNED NOT NULL DEFAULT 0,
  `rename_category` tinyint(1) UNSIGNED NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `trivia_channel_cache`;
CREATE TABLE `trivia_channel_cache` (
  `id` bigint(20) UNSIGNED NOT NULL,
  `guild_id` bigint(20) UNSIGNED NOT NULL,
  `parent_id` bigint(20) UNSIGNED DEFAULT NULL,
  `name` varchar(512) NOT NULL,
  `modified` datetime NOT NULL DEFAULT current_timestamp(),
  `type` int(11) UNSIGNED NOT NULL,
  `position` int(11) UNSIGNED NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `trivia_graphs`;
CREATE TABLE `trivia_graphs` (
  `id` bigint(20) UNSIGNED NOT NULL,
  `entry_date` datetime NOT NULL,
  `server_count` bigint(20) NOT NULL,
  `user_count` bigint(20) NOT NULL,
  `channel_count` bigint(20) NOT NULL,
  `cpu` decimal(5,2) NOT NULL,
  `memory_usage` bigint(20) NOT NULL,
  `games` bigint(20) NOT NULL DEFAULT 0 COMMENT 'Number of concurrent active games in progress',
  `discord_ping` decimal(8,3) NOT NULL,
  `trivia_ping` decimal(8,3) NOT NULL,
  `db_ping` decimal(8,3) NOT NULL,
  `kicks` bigint(20) UNSIGNED DEFAULT 0,
  `commands` bigint(20) UNSIGNED DEFAULT 0,
  `questions` bigint(20) UNSIGNED DEFAULT 0,
  `question_total` bigint(20) UNSIGNED NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `trivia_guild_cache`;
CREATE TABLE `trivia_guild_cache` (
  `snowflake_id` bigint(20) UNSIGNED NOT NULL COMMENT 'Snowflake ID PK',
  `name` text NOT NULL,
  `icon` varchar(255) NOT NULL,
  `owner_id` bigint(20) UNSIGNED NOT NULL COMMENT 'Owner ID FK',
  `joindate` datetime NOT NULL DEFAULT current_timestamp() COMMENT 'Date of first joining bot',
  `kicked` tinyint(1) UNSIGNED NOT NULL DEFAULT 0,
  `introsent` tinyint(1) UNSIGNED NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `trivia_guild_membership`;
CREATE TABLE `trivia_guild_membership` (
  `user_id` bigint(20) UNSIGNED NOT NULL,
  `guild_id` bigint(20) UNSIGNED NOT NULL,
  `nickname` varchar(760) DEFAULT NULL,
  `roles` text CHARACTER SET ascii DEFAULT NULL COMMENT 'Space separated list of roles for the user on the guild'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `trivia_role_cache`;
CREATE TABLE `trivia_role_cache` (
  `id` bigint(20) UNSIGNED NOT NULL COMMENT 'Role snowflake ID (PK)',
  `guild_id` bigint(20) UNSIGNED NOT NULL,
  `colour` int(11) UNSIGNED NOT NULL COMMENT 'Role colour (RGB)',
  `permissions` bigint(20) NOT NULL,
  `hoist` tinyint(1) UNSIGNED NOT NULL DEFAULT 0,
  `managed` tinyint(1) UNSIGNED NOT NULL DEFAULT 0,
  `mentionable` tinyint(1) UNSIGNED NOT NULL DEFAULT 0,
  `position` int(11) NOT NULL,
  `name` varchar(255) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Cache of guild roles';

DROP TABLE IF EXISTS `trivia_user_cache`;
CREATE TABLE `trivia_user_cache` (
  `snowflake_id` bigint(20) UNSIGNED NOT NULL COMMENT 'Snowflake ID PK',
  `username` varchar(700) NOT NULL,
  `discriminator` int(4) UNSIGNED ZEROFILL NOT NULL,
  `icon` varchar(256) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
DROP VIEW IF EXISTS `vw_activity`;
CREATE TABLE `vw_activity` (
`guild_id` bigint(20) unsigned
,`name` text
,`weekly_score` decimal(42,0)
);
DROP VIEW IF EXISTS `vw_biggest_servers`;
CREATE TABLE `vw_biggest_servers` (
`guild_id` bigint(20) unsigned
,`NAME` text
,`joindate` datetime
,`player_count` bigint(21)
);
DROP VIEW IF EXISTS `vw_change_per_15_minutes`;
CREATE TABLE `vw_change_per_15_minutes` (
`entry_date` datetime
,`daily_change` bigint(21)
);
DROP VIEW IF EXISTS `vw_change_per_day`;
CREATE TABLE `vw_change_per_day` (
`entry_date` datetime
,`daily_change` decimal(42,0)
);
DROP VIEW IF EXISTS `vw_consolidated_graph`;
CREATE TABLE `vw_consolidated_graph` (
`entry_date` datetime
,`server_count` decimal(19,0)
,`user_count` decimal(19,0)
,`channel_count` decimal(19,0)
,`cpu` decimal(5,2)
,`memory_usage` bigint(20)
,`games` decimal(19,0)
,`discord_ping` decimal(8,3)
,`trivia_ping` decimal(8,3)
,`db_ping` decimal(8,3)
,`kicks` decimal(20,0)
,`commands` decimal(20,0)
,`questions` decimal(20,0)
,`question_total` decimal(20,0)
);
DROP VIEW IF EXISTS `vw_consolidate_day`;
CREATE TABLE `vw_consolidate_day` (
`entry_date` datetime
,`server_count` bigint(20)
,`user_count` bigint(20)
,`channel_count` bigint(20)
,`cpu` decimal(5,2)
,`memory_usage` bigint(20)
,`games` bigint(20)
,`discord_ping` decimal(8,3)
,`trivia_ping` decimal(8,3)
,`db_ping` decimal(8,3)
,`kicks` bigint(20) unsigned
,`commands` bigint(20) unsigned
,`questions` bigint(20) unsigned
,`question_total` bigint(20) unsigned
);
DROP VIEW IF EXISTS `vw_consolidate_hour`;
CREATE TABLE `vw_consolidate_hour` (
`entry_date` datetime
,`server_count` bigint(20)
,`user_count` bigint(20)
,`channel_count` bigint(20)
,`cpu` decimal(5,2)
,`memory_usage` bigint(20)
,`games` bigint(20)
,`discord_ping` decimal(8,3)
,`trivia_ping` decimal(8,3)
,`db_ping` decimal(8,3)
,`kicks` bigint(20) unsigned
,`commands` bigint(20) unsigned
,`questions` bigint(20) unsigned
,`question_total` bigint(20) unsigned
);
DROP VIEW IF EXISTS `vw_consolidate_month`;
CREATE TABLE `vw_consolidate_month` (
`entry_date` datetime
,`server_count` bigint(20)
,`user_count` bigint(20)
,`channel_count` bigint(20)
,`cpu` decimal(5,2)
,`memory_usage` bigint(20)
,`games` bigint(20)
,`discord_ping` decimal(8,3)
,`trivia_ping` decimal(8,3)
,`db_ping` decimal(8,3)
,`kicks` bigint(20) unsigned
,`commands` bigint(20) unsigned
,`questions` bigint(20) unsigned
,`question_total` bigint(20) unsigned
);
DROP VIEW IF EXISTS `vw_contributions`;
CREATE TABLE `vw_contributions` (
`username` varchar(700)
,`count(id)` bigint(21)
);
DROP VIEW IF EXISTS `vw_conversion`;
CREATE TABLE `vw_conversion` (
`conversion_percent` decimal(27,4)
);
DROP VIEW IF EXISTS `vw_feedback`;
CREATE TABLE `vw_feedback` (
`id` bigint(20) unsigned
,`question_id` bigint(20) unsigned
,`insane_id` bigint(20) unsigned
,`user_id` bigint(20) unsigned
,`feedback` mediumtext
,`closed_by` bigint(20) unsigned
,`submitted` datetime
,`mod_notes` mediumtext
,`question` mediumtext
,`category` varchar(20)
,`type` varchar(6)
);
DROP VIEW IF EXISTS `vw_global_score`;
CREATE TABLE `vw_global_score` (
`score` decimal(42,0)
,`username` varchar(700)
,`discriminator` int(4) unsigned zerofill
);
DROP VIEW IF EXISTS `vw_lang_use`;
CREATE TABLE `vw_lang_use` (
`language` varchar(255)
,`total` bigint(21)
);
DROP VIEW IF EXISTS `vw_monthly_totals`;
CREATE TABLE `vw_monthly_totals` (
`score` decimal(42,0)
,`name` bigint(20) unsigned
);
DROP VIEW IF EXISTS `vw_prefix_use`;
CREATE TABLE `vw_prefix_use` (
`prefix` varchar(64)
,`count` bigint(21)
);
DROP VIEW IF EXISTS `vw_qsearch`;
CREATE TABLE `vw_qsearch` (
`id` bigint(20) unsigned
,`question` varchar(512)
,`category` bigint(20) unsigned
,`answer` longtext
,`hint1` longtext
,`hint2` longtext
,`category_name` varchar(256)
);
DROP VIEW IF EXISTS `vw_scorechart`;
CREATE TABLE `vw_scorechart` (
`score` decimal(42,0)
,`name` bigint(20) unsigned
);
DROP VIEW IF EXISTS `vw_seven_day_retention`;
CREATE TABLE `vw_seven_day_retention` (
`retention` decimal(28,4)
);
DROP VIEW IF EXISTS `vw_totals`;
CREATE TABLE `vw_totals` (
`score` decimal(42,0)
,`name` bigint(20) unsigned
);
DROP VIEW IF EXISTS `vw_translate_percent`;
CREATE TABLE `vw_translate_percent` (
`completion` decimal(27,4)
);
DROP VIEW IF EXISTS `vw_translate_percent_ia`;
CREATE TABLE `vw_translate_percent_ia` (
`completion` decimal(27,4)
);
DROP VIEW IF EXISTS `vw_weekly_active_guild_count`;
CREATE TABLE `vw_weekly_active_guild_count` (
`active_guilds` bigint(21)
);

DROP TABLE IF EXISTS `warnings`;
CREATE TABLE `warnings` (
  `id` bigint(20) UNSIGNED NOT NULL,
  `user_id` bigint(20) UNSIGNED NOT NULL,
  `question` text DEFAULT NULL,
  `answer` text DEFAULT NULL,
  `user_feedback` text DEFAULT NULL,
  `question_id` bigint(20) UNSIGNED DEFAULT NULL,
  `insane_id` bigint(20) UNSIGNED DEFAULT NULL,
  `reason` text NOT NULL,
  `warn_date` datetime NOT NULL DEFAULT current_timestamp(),
  `warned_by_id` bigint(20) UNSIGNED NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Contains a list of warnings (trollish questions submitted)';
DROP TABLE IF EXISTS `vw_activity`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY INVOKER VIEW `vw_activity`  AS  select `scores`.`guild_id` AS `guild_id`,`trivia_guild_cache`.`name` AS `name`,sum(`scores`.`weekscore`) AS `weekly_score` from (`scores` join `trivia_guild_cache` on(`scores`.`guild_id` = `trivia_guild_cache`.`snowflake_id`)) where `trivia_guild_cache`.`kicked` = 0 group by `scores`.`guild_id` order by sum(`scores`.`weekscore`) desc ;
DROP TABLE IF EXISTS `vw_biggest_servers`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY INVOKER VIEW `vw_biggest_servers`  AS  select `trivia_guild_membership`.`guild_id` AS `guild_id`,`trivia_guild_cache`.`name` AS `NAME`,`trivia_guild_cache`.`joindate` AS `joindate`,count(`trivia_guild_membership`.`user_id`) AS `player_count` from (`trivia_guild_membership` join `trivia_guild_cache` on(`trivia_guild_membership`.`guild_id` = `trivia_guild_cache`.`snowflake_id`)) where `trivia_guild_cache`.`kicked` = 0 group by `trivia_guild_membership`.`guild_id` order by count(`trivia_guild_membership`.`user_id`) desc limit 25 ;
DROP TABLE IF EXISTS `vw_change_per_15_minutes`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY DEFINER VIEW `vw_change_per_15_minutes`  AS  select `trivia_graphs`.`entry_date` AS `entry_date`,`trivia_graphs`.`server_count` - lag(`trivia_graphs`.`server_count`,1) over ( order by `trivia_graphs`.`entry_date`) AS `daily_change` from `trivia_graphs` where `trivia_graphs`.`entry_date` > current_timestamp() - interval 3 month ;
DROP TABLE IF EXISTS `vw_change_per_day`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY INVOKER VIEW `vw_change_per_day`  AS  select `vw_change_per_15_minutes`.`entry_date` AS `entry_date`,sum(`vw_change_per_15_minutes`.`daily_change`) AS `daily_change` from `vw_change_per_15_minutes` group by unix_timestamp(`vw_change_per_15_minutes`.`entry_date`) DIV 86400 order by `vw_change_per_15_minutes`.`entry_date` ;
DROP TABLE IF EXISTS `vw_consolidated_graph`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY INVOKER VIEW `vw_consolidated_graph`  AS  select `trivia_graphs`.`entry_date` AS `entry_date`,ceiling(`trivia_graphs`.`server_count`) AS `server_count`,ceiling(`trivia_graphs`.`user_count`) AS `user_count`,ceiling(`trivia_graphs`.`channel_count`) AS `channel_count`,`trivia_graphs`.`cpu` AS `cpu`,`trivia_graphs`.`memory_usage` AS `memory_usage`,ceiling(`trivia_graphs`.`games`) AS `games`,`trivia_graphs`.`discord_ping` AS `discord_ping`,`trivia_graphs`.`trivia_ping` AS `trivia_ping`,`trivia_graphs`.`db_ping` AS `db_ping`,ceiling(`trivia_graphs`.`kicks`) AS `kicks`,ceiling(`trivia_graphs`.`commands`) AS `commands`,ceiling(`trivia_graphs`.`questions`) AS `questions`,ceiling(`trivia_graphs`.`question_total`) AS `question_total` from `trivia_graphs` where unix_timestamp(`trivia_graphs`.`entry_date`) between unix_timestamp() - 86400 and unix_timestamp() union select `vw_consolidate_hour`.`entry_date` AS `entry_date`,ceiling(`vw_consolidate_hour`.`server_count`) AS `server_count`,ceiling(`vw_consolidate_hour`.`user_count`) AS `user_count`,ceiling(`vw_consolidate_hour`.`channel_count`) AS `channel_count`,`vw_consolidate_hour`.`cpu` AS `cpu`,`vw_consolidate_hour`.`memory_usage` AS `memory_usage`,ceiling(`vw_consolidate_hour`.`games`) AS `games`,`vw_consolidate_hour`.`discord_ping` AS `discord_ping`,`vw_consolidate_hour`.`trivia_ping` AS `trivia_ping`,`vw_consolidate_hour`.`db_ping` AS `db_ping`,ceiling(`vw_consolidate_hour`.`kicks`) AS `kicks`,ceiling(`vw_consolidate_hour`.`commands`) AS `commands`,ceiling(`vw_consolidate_hour`.`questions`) AS `questions`,ceiling(`vw_consolidate_hour`.`question_total`) AS `question_total` from `vw_consolidate_hour` where unix_timestamp(`vw_consolidate_hour`.`entry_date`) between unix_timestamp() - 86400 * 7 and unix_timestamp() - 86400 union select `vw_consolidate_day`.`entry_date` AS `entry_date`,ceiling(`vw_consolidate_day`.`server_count`) AS `server_count`,ceiling(`vw_consolidate_day`.`user_count`) AS `user_count`,ceiling(`vw_consolidate_day`.`channel_count`) AS `channel_count`,`vw_consolidate_day`.`cpu` AS `cpu`,`vw_consolidate_day`.`memory_usage` AS `memory_usage`,ceiling(`vw_consolidate_day`.`games`) AS `games`,`vw_consolidate_day`.`discord_ping` AS `discord_ping`,`vw_consolidate_day`.`trivia_ping` AS `trivia_ping`,`vw_consolidate_day`.`db_ping` AS `db_ping`,ceiling(`vw_consolidate_day`.`kicks`) AS `kicks`,ceiling(`vw_consolidate_day`.`commands`) AS `commands`,ceiling(`vw_consolidate_day`.`questions`) AS `questions`,ceiling(`vw_consolidate_day`.`question_total`) AS `question_total` from `vw_consolidate_day` where unix_timestamp(`vw_consolidate_day`.`entry_date`) between unix_timestamp() - 86400 * 90 and unix_timestamp() - 86400 * 7 union select `vw_consolidate_month`.`entry_date` AS `entry_date`,ceiling(`vw_consolidate_month`.`server_count`) AS `server_count`,ceiling(`vw_consolidate_month`.`user_count`) AS `user_count`,ceiling(`vw_consolidate_month`.`channel_count`) AS `channel_count`,`vw_consolidate_month`.`cpu` AS `cpu`,`vw_consolidate_month`.`memory_usage` AS `memory_usage`,ceiling(`vw_consolidate_month`.`games`) AS `games`,`vw_consolidate_month`.`discord_ping` AS `discord_ping`,`vw_consolidate_month`.`trivia_ping` AS `trivia_ping`,`vw_consolidate_month`.`db_ping` AS `db_ping`,ceiling(`vw_consolidate_month`.`kicks`) AS `kicks`,ceiling(`vw_consolidate_month`.`commands`) AS `commands`,ceiling(`vw_consolidate_month`.`questions`) AS `questions`,ceiling(`vw_consolidate_month`.`question_total`) AS `question_total` from `vw_consolidate_month` where unix_timestamp(`vw_consolidate_month`.`entry_date`) <= unix_timestamp() - 86400 * 90 order by `entry_date` ;
DROP TABLE IF EXISTS `vw_consolidate_day`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY DEFINER VIEW `vw_consolidate_day`  AS  select `trivia_graphs`.`entry_date` AS `entry_date`,last_value(`trivia_graphs`.`server_count`) AS `server_count`,last_value(`trivia_graphs`.`user_count`) AS `user_count`,last_value(`trivia_graphs`.`channel_count`) AS `channel_count`,last_value(`trivia_graphs`.`cpu`) AS `cpu`,last_value(`trivia_graphs`.`memory_usage`) AS `memory_usage`,last_value(`trivia_graphs`.`games`) AS `games`,last_value(`trivia_graphs`.`discord_ping`) AS `discord_ping`,last_value(`trivia_graphs`.`trivia_ping`) AS `trivia_ping`,last_value(`trivia_graphs`.`db_ping`) AS `db_ping`,last_value(`trivia_graphs`.`kicks`) AS `kicks`,last_value(`trivia_graphs`.`commands`) AS `commands`,last_value(`trivia_graphs`.`questions`) AS `questions`,last_value(`trivia_graphs`.`question_total`) AS `question_total` from `trivia_graphs` group by unix_timestamp(`trivia_graphs`.`entry_date`) DIV 86400 order by `trivia_graphs`.`entry_date` ;
DROP TABLE IF EXISTS `vw_consolidate_hour`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY DEFINER VIEW `vw_consolidate_hour`  AS  select `trivia_graphs`.`entry_date` AS `entry_date`,last_value(`trivia_graphs`.`server_count`) AS `server_count`,last_value(`trivia_graphs`.`user_count`) AS `user_count`,last_value(`trivia_graphs`.`channel_count`) AS `channel_count`,last_value(`trivia_graphs`.`cpu`) AS `cpu`,last_value(`trivia_graphs`.`memory_usage`) AS `memory_usage`,last_value(`trivia_graphs`.`games`) AS `games`,last_value(`trivia_graphs`.`discord_ping`) AS `discord_ping`,last_value(`trivia_graphs`.`trivia_ping`) AS `trivia_ping`,last_value(`trivia_graphs`.`db_ping`) AS `db_ping`,last_value(`trivia_graphs`.`kicks`) AS `kicks`,last_value(`trivia_graphs`.`commands`) AS `commands`,last_value(`trivia_graphs`.`questions`) AS `questions`,last_value(`trivia_graphs`.`question_total`) AS `question_total` from `trivia_graphs` group by unix_timestamp(`trivia_graphs`.`entry_date`) DIV 3600 order by `trivia_graphs`.`entry_date` ;
DROP TABLE IF EXISTS `vw_consolidate_month`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY DEFINER VIEW `vw_consolidate_month`  AS  select `trivia_graphs`.`entry_date` AS `entry_date`,last_value(`trivia_graphs`.`server_count`) AS `server_count`,last_value(`trivia_graphs`.`user_count`) AS `user_count`,last_value(`trivia_graphs`.`channel_count`) AS `channel_count`,last_value(`trivia_graphs`.`cpu`) AS `cpu`,last_value(`trivia_graphs`.`memory_usage`) AS `memory_usage`,last_value(`trivia_graphs`.`games`) AS `games`,last_value(`trivia_graphs`.`discord_ping`) AS `discord_ping`,last_value(`trivia_graphs`.`trivia_ping`) AS `trivia_ping`,last_value(`trivia_graphs`.`db_ping`) AS `db_ping`,last_value(`trivia_graphs`.`kicks`) AS `kicks`,last_value(`trivia_graphs`.`commands`) AS `commands`,last_value(`trivia_graphs`.`questions`) AS `questions`,last_value(`trivia_graphs`.`question_total`) AS `question_total` from `trivia_graphs` group by unix_timestamp(`trivia_graphs`.`entry_date`) DIV (86400 * 30) order by `trivia_graphs`.`entry_date` ;
DROP TABLE IF EXISTS `vw_contributions`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY INVOKER VIEW `vw_contributions`  AS  select `trivia_user_cache`.`username` AS `username`,count(`questions`.`id`) AS `count(id)` from (`questions` left join `trivia_user_cache` on(`questions`.`submitted_by` = `trivia_user_cache`.`snowflake_id`)) group by `questions`.`submitted_by` order by count(`questions`.`id`) desc ;
DROP TABLE IF EXISTS `vw_conversion`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY INVOKER VIEW `vw_conversion`  AS  select (select count(`premium_credits`.`user_id`) from `premium_credits` where `premium_credits`.`active` = 1) / (select `vw_weekly_active_guild_count`.`active_guilds` from `vw_weekly_active_guild_count`) * 100 AS `conversion_percent` ;
DROP TABLE IF EXISTS `vw_feedback`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY INVOKER VIEW `vw_feedback`  AS  select `feedback`.`id` AS `id`,`feedback`.`question_id` AS `question_id`,`feedback`.`insane_id` AS `insane_id`,`feedback`.`user_id` AS `user_id`,`feedback`.`feedback` AS `feedback`,`feedback`.`closed_by` AS `closed_by`,`feedback`.`submitted` AS `submitted`,`feedback`.`mod_notes` AS `mod_notes`,`questions`.`question` AS `question`,'Insane Round' AS `category`,'insane' AS `type` from (`feedback` left join `insane` `questions` on(`feedback`.`insane_id` = `questions`.`id`)) where `feedback`.`insane_id` is not null union select `feedback`.`id` AS `id`,`feedback`.`question_id` AS `question_id`,`feedback`.`insane_id` AS `insane_id`,`feedback`.`user_id` AS `user_id`,`feedback`.`feedback` AS `feedback`,`feedback`.`closed_by` AS `closed_by`,`feedback`.`submitted` AS `submitted`,`feedback`.`mod_notes` AS `mod_notes`,`questions`.`question` AS `question`,`questions`.`category` AS `category`,'normal' AS `type` from (`feedback` left join `questions` on(`feedback`.`question_id` = `questions`.`id`)) where `feedback`.`insane_id` is null ;
DROP TABLE IF EXISTS `vw_global_score`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY DEFINER VIEW `vw_global_score`  AS  select `vw_totals`.`score` AS `score`,`trivia_user_cache`.`username` AS `username`,`trivia_user_cache`.`discriminator` AS `discriminator` from (`vw_totals` join `trivia_user_cache` on(`trivia_user_cache`.`snowflake_id` = `vw_totals`.`name`)) order by `vw_totals`.`score` desc ;
DROP TABLE IF EXISTS `vw_lang_use`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY INVOKER VIEW `vw_lang_use`  AS  select `languages`.`name` AS `language`,count(`bot_guild_settings`.`snowflake_id`) AS `total` from ((`bot_guild_settings` join `languages` on(`languages`.`isocode` = `bot_guild_settings`.`language`)) join `trivia_guild_cache` on(`trivia_guild_cache`.`snowflake_id` = `bot_guild_settings`.`snowflake_id`)) where `bot_guild_settings`.`language` <> 'en' and `trivia_guild_cache`.`kicked` = 0 group by `languages`.`name` order by `languages`.`name` ;
DROP TABLE IF EXISTS `vw_monthly_totals`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY DEFINER VIEW `vw_monthly_totals`  AS  select sum(`scores`.`monthscore`) AS `score`,`scores`.`name` AS `name` from `scores` group by `scores`.`name` ;
DROP TABLE IF EXISTS `vw_prefix_use`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY INVOKER VIEW `vw_prefix_use`  AS  select `bot_guild_settings`.`prefix` AS `prefix`,count(`bot_guild_settings`.`prefix`) AS `count` from (`bot_guild_settings` join `trivia_guild_cache` on(`trivia_guild_cache`.`snowflake_id` = `bot_guild_settings`.`snowflake_id`)) where `trivia_guild_cache`.`kicked` = 0 group by `bot_guild_settings`.`prefix` having count(`bot_guild_settings`.`prefix`) > 4 and `bot_guild_settings`.`prefix` <> '!' and `bot_guild_settings`.`prefix` <> '' ;
DROP TABLE IF EXISTS `vw_qsearch`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY INVOKER VIEW `vw_qsearch`  AS  select `questions`.`id` AS `id`,`questions`.`question` AS `question`,`questions`.`category` AS `category`,`answers`.`answer` AS `answer`,`hints`.`hint1` AS `hint1`,`hints`.`hint2` AS `hint2`,`categories`.`name` AS `category_name` from (((`questions` join `categories` on(`questions`.`category` = `categories`.`id`)) join `answers` on(`questions`.`id` = `answers`.`id`)) join `hints` on(`hints`.`id` = `questions`.`id`)) where `questions`.`category` > 0 ;
DROP TABLE IF EXISTS `vw_scorechart`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY DEFINER VIEW `vw_scorechart`  AS  select sum(`scores`.`score`) AS `score`,`scores`.`name` AS `name` from `scores` group by `scores`.`name` ;
DROP TABLE IF EXISTS `vw_seven_day_retention`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY INVOKER VIEW `vw_seven_day_retention`  AS  select 100 - `trivia_graphs`.`kicks` / (`trivia_graphs`.`server_count` - lag(`trivia_graphs`.`server_count`,1) over ( order by `trivia_graphs`.`entry_date`)) * 100 AS `retention` from `trivia_graphs` where `trivia_graphs`.`entry_date` > current_timestamp() - interval 7 day ;
DROP TABLE IF EXISTS `vw_totals`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY DEFINER VIEW `vw_totals`  AS  select sum(`scores`.`score`) AS `score`,`scores`.`name` AS `name` from `scores` group by `scores`.`name` ;
DROP TABLE IF EXISTS `vw_translate_percent`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY DEFINER VIEW `vw_translate_percent`  AS  select count(`questions`.`id`) / (select count(0) from `questions`) * 100 AS `completion` from `questions` where `questions`.`trans_zh` is not null ;
DROP TABLE IF EXISTS `vw_translate_percent_ia`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY DEFINER VIEW `vw_translate_percent_ia`  AS  select count(`insane_answers`.`id`) / (select count(0) from `insane_answers`) * 100 AS `completion` from `insane_answers` where `insane_answers`.`trans_zh` is not null ;
DROP TABLE IF EXISTS `vw_weekly_active_guild_count`;

CREATE ALGORITHM=UNDEFINED DEFINER=`admin`@`localhost` SQL SECURITY INVOKER VIEW `vw_weekly_active_guild_count`  AS  select count(`vw_activity`.`guild_id`) AS `active_guilds` from `vw_activity` where `vw_activity`.`weekly_score` > 0 ;


ALTER TABLE `achievements`
  ADD PRIMARY KEY (`user_id`,`achievement_id`) USING BTREE,
  ADD KEY `achievement_id` (`achievement_id`),
  ADD KEY `unlocked` (`unlocked`);

ALTER TABLE `active_games`
  ADD PRIMARY KEY (`guild_id`,`channel_id`,`hostname`),
  ADD KEY `quickfire` (`quickfire`),
  ADD KEY `started` (`started`),
  ADD KEY `user_id` (`user_id`),
  ADD KEY `stop` (`stop`),
  ADD KEY `state` (`state`),
  ADD KEY `cluster_id` (`cluster_id`);

ALTER TABLE `active_roles_rewarded`
  ADD PRIMARY KEY (`guild_id`,`user_id`),
  ADD KEY `role_id` (`role_id`),
  ADD KEY `user_id` (`user_id`);

ALTER TABLE `answers`
  ADD PRIMARY KEY (`id`),
  ADD KEY `index2` (`answer_img_url`);

ALTER TABLE `bot_guild_settings`
  ADD PRIMARY KEY (`snowflake_id`),
  ADD UNIQUE KEY `custom_url` (`custom_url`),
  ADD KEY `premium` (`premium`),
  ADD KEY `only_mods_stop` (`only_mods_stop`),
  ADD KEY `role_reward_enabled` (`role_reward_enabled`),
  ADD KEY `role_reward_id` (`role_reward_id`),
  ADD KEY `points_reward_role_id` (`points_reward_role_id`),
  ADD KEY `language` (`language`);

ALTER TABLE `categories`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `name` (`name`) USING BTREE,
  ADD KEY `disabled` (`disabled`),
  ADD KEY `weight` (`weight`),
  ADD KEY `translation_dirty` (`translation_dirty`);

ALTER TABLE `disabled_categories`
  ADD PRIMARY KEY (`guild_id`,`category_id`),
  ADD KEY `guild_id` (`guild_id`),
  ADD KEY `category_id` (`category_id`);

ALTER TABLE `dm_queue`
  ADD PRIMARY KEY (`id`),
  ADD KEY `snowflake_id` (`snowflake_id`);

ALTER TABLE `feedback`
  ADD PRIMARY KEY (`id`),
  ADD KEY `question_id` (`question_id`),
  ADD KEY `insane_id` (`insane_id`),
  ADD KEY `user_id` (`user_id`),
  ADD KEY `closed_by` (`closed_by`),
  ADD KEY `submitted` (`submitted`);

ALTER TABLE `game_score_history`
  ADD PRIMARY KEY (`url_key`) USING BTREE,
  ADD KEY `timestarted` (`timestarted`),
  ADD KEY `timefinished` (`timefinished`),
  ADD KEY `guild_id` (`guild_id`,`timestarted`);

ALTER TABLE `hints`
  ADD PRIMARY KEY (`id`);

ALTER TABLE `infobot_bandwidth`
  ADD PRIMARY KEY (`logdate`);

ALTER TABLE `infobot_discord_counts`
  ADD PRIMARY KEY (`shard_id`,`dev`,`cluster_id`) USING BTREE,
  ADD KEY `last_updated` (`last_updated`),
  ADD KEY `cluster_id` (`cluster_id`);

ALTER TABLE `infobot_discord_list_sites`
  ADD PRIMARY KEY (`id`),
  ADD KEY `name` (`name`),
  ADD KEY `post_type` (`post_type`);

ALTER TABLE `infobot_shard_status`
  ADD PRIMARY KEY (`id`),
  ADD KEY `connected` (`connected`),
  ADD KEY `online` (`online`),
  ADD KEY `uptimerobot_heartbeat` (`uptimerobot_heartbeat`),
  ADD KEY `forcereconnect` (`forcereconnect`),
  ADD KEY `uptimerobot_id` (`uptimerobot_id`),
  ADD KEY `cluster_id` (`cluster_id`);

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

ALTER TABLE `insane`
  ADD PRIMARY KEY (`id`),
  ADD KEY `translation_dirty` (`translation_dirty`),
  ADD KEY `deleted` (`deleted`);

ALTER TABLE `insane_answers`
  ADD PRIMARY KEY (`id`),
  ADD KEY `fk_qi` (`question_id`),
  ADD KEY `translation_dirty` (`translation_dirty`);

ALTER TABLE `insane_round_statistics`
  ADD PRIMARY KEY (`guild_id`,`user_id`) USING BTREE,
  ADD KEY `score` (`score`),
  ADD KEY `channel_id` (`channel_id`),
  ADD KEY `user_id` (`user_id`);

ALTER TABLE `languages`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `isocode` (`isocode`),
  ADD KEY `live` (`live`),
  ADD KEY `altcode` (`altcode`);

ALTER TABLE `new_daywinners`
  ADD PRIMARY KEY (`guild_id`,`datestamp`),
  ADD KEY `user_id` (`user_id`),
  ADD KEY `datestamp` (`datestamp`);

ALTER TABLE `numstrs`
  ADD PRIMARY KEY (`id`),
  ADD KEY `translation_dirty` (`translation_dirty`),
  ADD KEY `value` (`value`) USING BTREE;

ALTER TABLE `premium_credits`
  ADD PRIMARY KEY (`user_id`,`subscription_id`),
  ADD KEY `active` (`active`),
  ADD KEY `since` (`since`),
  ADD KEY `fk_plan` (`plan_id`),
  ADD KEY `guild_id` (`guild_id`) USING BTREE;

ALTER TABLE `premium_plans`
  ADD PRIMARY KEY (`id`),
  ADD KEY `lifetime` (`lifetime`),
  ADD KEY `period` (`period`),
  ADD KEY `period_unit` (`period_unit`),
  ADD KEY `cache_date` (`cache_date`);

ALTER TABLE `questions`
  ADD PRIMARY KEY (`id`) USING BTREE,
  ADD KEY `question` (`question`),
  ADD KEY `fk_cat` (`category`),
  ADD KEY `submitted_by` (`submitted_by`),
  ADD KEY `translation_dirty` (`translation_dirty`),
  ADD KEY `approved_date` (`approved_date`),
  ADD KEY `last_edited_date` (`last_edited_date`),
  ADD KEY `last_edited_by` (`last_edited_by`),
  ADD KEY `approved_by` (`approved_by`),
  ADD KEY `index10` (`question_img_url`);

ALTER TABLE `question_queue`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `question` (`question`,`answer`),
  ADD KEY `dateadded` (`dateadded`),
  ADD KEY `snowflake_id` (`snowflake_id`),
  ADD KEY `category` (`category`);

ALTER TABLE `role_change_queue`
  ADD PRIMARY KEY (`id`),
  ADD KEY `adding` (`adding`),
  ADD KEY `guild_id` (`guild_id`),
  ADD KEY `user_id` (`user_id`),
  ADD KEY `role_id` (`role_id`);

ALTER TABLE `scheduled_games`
  ADD PRIMARY KEY (`id`),
  ADD KEY `user_id` (`user_id`),
  ADD KEY `start_time` (`start_time`),
  ADD KEY `guild_id` (`guild_id`),
  ADD KEY `channel_id` (`channel_id`),
  ADD KEY `queuetime` (`queuetime`);

ALTER TABLE `scores`
  ADD PRIMARY KEY (`name`,`guild_id`) USING BTREE,
  ADD KEY `score` (`score`),
  ADD KEY `dayscore` (`dayscore`),
  ADD KEY `weekscore` (`weekscore`),
  ADD KEY `monthscore` (`monthscore`),
  ADD KEY `guild_id` (`guild_id`);

ALTER TABLE `scores_lastgame`
  ADD PRIMARY KEY (`guild_id`,`user_id`),
  ADD KEY `guild_id` (`guild_id`),
  ADD KEY `user_id` (`user_id`),
  ADD KEY `guild_id_2` (`guild_id`,`user_id`,`score`);

ALTER TABLE `start_log`
  ADD PRIMARY KEY (`user_id`),
  ADD KEY `last_started` (`last_started`);

ALTER TABLE `start_queue`
  ADD PRIMARY KEY (`guild_id`,`channel_id`),
  ADD KEY `user_id` (`user_id`),
  ADD KEY `queuetime` (`queuetime`);

ALTER TABLE `stats`
  ADD PRIMARY KEY (`id`),
  ADD KEY `lastcorrect` (`lastcorrect`);

ALTER TABLE `streaks`
  ADD PRIMARY KEY (`nick`,`guild_id`) USING BTREE,
  ADD KEY `streak` (`streak`),
  ADD KEY `fk_guild_id` (`guild_id`);

ALTER TABLE `teams`
  ADD PRIMARY KEY (`name`),
  ADD UNIQUE KEY `url_key` (`url_key`),
  ADD UNIQUE KEY `team_url_2` (`team_url`),
  ADD KEY `score` (`score`),
  ADD KEY `owner_id` (`owner_id`),
  ADD KEY `create_date` (`create_date`),
  ADD KEY `discord_invite` (`discord_invite`),
  ADD KEY `show_member_list` (`show_member_list`);

ALTER TABLE `team_membership`
  ADD PRIMARY KEY (`nick`),
  ADD KEY `fk_team` (`team`),
  ADD KEY `privacy_mode` (`privacy_mode`),
  ADD KEY `team_manager` (`team_manager`),
  ADD KEY `joined` (`joined`),
  ADD KEY `points_contributed` (`points_contributed`);

ALTER TABLE `trivia_access`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `user_id` (`user_id`) USING BTREE,
  ADD UNIQUE KEY `key` (`api_key`) USING BTREE,
  ADD KEY `enabled` (`enabled`),
  ADD KEY `review_queue` (`review_queue`),
  ADD KEY `restart_bot` (`restart_bot`),
  ADD KEY `game_control` (`game_control`),
  ADD KEY `delete_question` (`delete_question`),
  ADD KEY `add_category` (`add_category`),
  ADD KEY `add_question` (`add_question`),
  ADD KEY `rename_category` (`rename_category`);

ALTER TABLE `trivia_channel_cache`
  ADD PRIMARY KEY (`id`),
  ADD KEY `modified` (`modified`),
  ADD KEY `guild_id` (`guild_id`),
  ADD KEY `parent_id` (`parent_id`),
  ADD KEY `name` (`name`),
  ADD KEY `position` (`position`),
  ADD KEY `type` (`type`);

ALTER TABLE `trivia_graphs`
  ADD PRIMARY KEY (`id`),
  ADD KEY `entry_date` (`entry_date`);

ALTER TABLE `trivia_guild_cache`
  ADD PRIMARY KEY (`snowflake_id`),
  ADD KEY `owner_id` (`owner_id`),
  ADD KEY `joindate` (`joindate`),
  ADD KEY `kicked` (`kicked`),
  ADD KEY `introsent` (`introsent`);

ALTER TABLE `trivia_guild_membership`
  ADD PRIMARY KEY (`user_id`,`guild_id`),
  ADD KEY `fk_guild` (`guild_id`);

ALTER TABLE `trivia_role_cache`
  ADD PRIMARY KEY (`id`),
  ADD KEY `guild_id` (`guild_id`),
  ADD KEY `position` (`position`);

ALTER TABLE `trivia_user_cache`
  ADD PRIMARY KEY (`snowflake_id`),
  ADD KEY `username` (`username`,`discriminator`),
  ADD KEY `username_2` (`username`);

ALTER TABLE `warnings`
  ADD PRIMARY KEY (`id`),
  ADD KEY `user_id` (`user_id`),
  ADD KEY `warn_date` (`warn_date`),
  ADD KEY `warned_by_id` (`warned_by_id`),
  ADD KEY `insane_id` (`insane_id`),
  ADD KEY `question_id` (`question_id`);


ALTER TABLE `categories`
  MODIFY `id` bigint(20) UNSIGNED NOT NULL AUTO_INCREMENT;

ALTER TABLE `dm_queue`
  MODIFY `id` bigint(20) UNSIGNED NOT NULL AUTO_INCREMENT;

ALTER TABLE `feedback`
  MODIFY `id` bigint(20) UNSIGNED NOT NULL AUTO_INCREMENT COMMENT 'primary key';

ALTER TABLE `infobot_discord_list_sites`
  MODIFY `id` bigint(20) UNSIGNED NOT NULL AUTO_INCREMENT;

ALTER TABLE `infobot_votes`
  MODIFY `id` bigint(20) UNSIGNED NOT NULL AUTO_INCREMENT;

ALTER TABLE `insane`
  MODIFY `id` bigint(20) UNSIGNED NOT NULL AUTO_INCREMENT;

ALTER TABLE `insane_answers`
  MODIFY `id` bigint(20) UNSIGNED NOT NULL AUTO_INCREMENT;

ALTER TABLE `languages`
  MODIFY `id` bigint(20) NOT NULL AUTO_INCREMENT;

ALTER TABLE `numstrs`
  MODIFY `id` bigint(20) UNSIGNED NOT NULL AUTO_INCREMENT;

ALTER TABLE `question_queue`
  MODIFY `id` bigint(20) NOT NULL AUTO_INCREMENT;

ALTER TABLE `role_change_queue`
  MODIFY `id` bigint(20) UNSIGNED NOT NULL AUTO_INCREMENT;

ALTER TABLE `scheduled_games`
  MODIFY `id` bigint(20) UNSIGNED NOT NULL AUTO_INCREMENT;

ALTER TABLE `trivia_access`
  MODIFY `id` bigint(20) UNSIGNED NOT NULL AUTO_INCREMENT COMMENT 'PK';

ALTER TABLE `trivia_graphs`
  MODIFY `id` bigint(20) UNSIGNED NOT NULL AUTO_INCREMENT;

ALTER TABLE `warnings`
  MODIFY `id` bigint(20) UNSIGNED NOT NULL AUTO_INCREMENT;


ALTER TABLE `achievements`
  ADD CONSTRAINT `achievements_ibfk_1` FOREIGN KEY (`user_id`) REFERENCES `trivia_user_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION;

ALTER TABLE `active_games`
  ADD CONSTRAINT `active_games_ibfk_1` FOREIGN KEY (`guild_id`) REFERENCES `trivia_guild_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION,
  ADD CONSTRAINT `active_games_ibfk_2` FOREIGN KEY (`user_id`) REFERENCES `trivia_user_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION;

ALTER TABLE `active_roles_rewarded`
  ADD CONSTRAINT `active_roles_rewarded_ibfk_1` FOREIGN KEY (`guild_id`) REFERENCES `trivia_guild_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION,
  ADD CONSTRAINT `active_roles_rewarded_ibfk_2` FOREIGN KEY (`role_id`) REFERENCES `trivia_role_cache` (`id`) ON DELETE NO ACTION ON UPDATE NO ACTION,
  ADD CONSTRAINT `active_roles_rewarded_ibfk_3` FOREIGN KEY (`user_id`) REFERENCES `trivia_user_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION;

ALTER TABLE `answers`
  ADD CONSTRAINT `fk_q` FOREIGN KEY (`id`) REFERENCES `questions` (`id`);

ALTER TABLE `disabled_categories`
  ADD CONSTRAINT `disabled_categories_ibfk_1` FOREIGN KEY (`category_id`) REFERENCES `categories` (`id`),
  ADD CONSTRAINT `disabled_categories_ibfk_2` FOREIGN KEY (`guild_id`) REFERENCES `trivia_guild_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION;

ALTER TABLE `dm_queue`
  ADD CONSTRAINT `dm_queue_ibfk_1` FOREIGN KEY (`snowflake_id`) REFERENCES `trivia_user_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION;

ALTER TABLE `feedback`
  ADD CONSTRAINT `feedback_ibfk_1` FOREIGN KEY (`insane_id`) REFERENCES `insane` (`id`),
  ADD CONSTRAINT `feedback_ibfk_2` FOREIGN KEY (`question_id`) REFERENCES `questions` (`id`),
  ADD CONSTRAINT `feedback_ibfk_3` FOREIGN KEY (`closed_by`) REFERENCES `trivia_user_cache` (`snowflake_id`),
  ADD CONSTRAINT `feedback_ibfk_4` FOREIGN KEY (`user_id`) REFERENCES `trivia_user_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION;

ALTER TABLE `game_score_history`
  ADD CONSTRAINT `game_score_history_ibfk_1` FOREIGN KEY (`guild_id`) REFERENCES `trivia_guild_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION;

ALTER TABLE `hints`
  ADD CONSTRAINT `fk_a` FOREIGN KEY (`id`) REFERENCES `answers` (`id`);

ALTER TABLE `infobot_votes`
  ADD CONSTRAINT `infobot_votes_ibfk_1` FOREIGN KEY (`origin`) REFERENCES `infobot_vote_links` (`site`);

ALTER TABLE `insane_answers`
  ADD CONSTRAINT `fk_qi` FOREIGN KEY (`question_id`) REFERENCES `insane` (`id`);

ALTER TABLE `insane_round_statistics`
  ADD CONSTRAINT `insane_round_statistics_ibfk_1` FOREIGN KEY (`guild_id`) REFERENCES `trivia_guild_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION,
  ADD CONSTRAINT `insane_round_statistics_ibfk_2` FOREIGN KEY (`user_id`) REFERENCES `trivia_user_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION;

ALTER TABLE `new_daywinners`
  ADD CONSTRAINT `new_daywinners_ibfk_1` FOREIGN KEY (`guild_id`) REFERENCES `trivia_guild_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION,
  ADD CONSTRAINT `new_daywinners_ibfk_2` FOREIGN KEY (`user_id`) REFERENCES `trivia_user_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION;

ALTER TABLE `premium_credits`
  ADD CONSTRAINT `fk_plan` FOREIGN KEY (`plan_id`) REFERENCES `premium_plans` (`id`),
  ADD CONSTRAINT `premium_credits_ibfk_1` FOREIGN KEY (`guild_id`) REFERENCES `trivia_guild_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION,
  ADD CONSTRAINT `premium_credits_ibfk_2` FOREIGN KEY (`user_id`) REFERENCES `trivia_user_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION;

ALTER TABLE `questions`
  ADD CONSTRAINT `fk_cat` FOREIGN KEY (`category`) REFERENCES `categories` (`id`),
  ADD CONSTRAINT `questions_ibfk_1` FOREIGN KEY (`submitted_by`) REFERENCES `trivia_user_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION,
  ADD CONSTRAINT `questions_ibfk_2` FOREIGN KEY (`last_edited_by`) REFERENCES `trivia_user_cache` (`snowflake_id`),
  ADD CONSTRAINT `questions_ibfk_3` FOREIGN KEY (`approved_by`) REFERENCES `trivia_user_cache` (`snowflake_id`);

ALTER TABLE `question_queue`
  ADD CONSTRAINT `question_queue_ibfk_1` FOREIGN KEY (`snowflake_id`) REFERENCES `trivia_user_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION;

ALTER TABLE `role_change_queue`
  ADD CONSTRAINT `role_change_queue_ibfk_1` FOREIGN KEY (`guild_id`) REFERENCES `trivia_guild_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION,
  ADD CONSTRAINT `role_change_queue_ibfk_2` FOREIGN KEY (`role_id`) REFERENCES `trivia_role_cache` (`id`) ON DELETE NO ACTION ON UPDATE NO ACTION,
  ADD CONSTRAINT `role_change_queue_ibfk_3` FOREIGN KEY (`user_id`) REFERENCES `trivia_user_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION;

ALTER TABLE `scheduled_games`
  ADD CONSTRAINT `scheduled_games_ibfk_1` FOREIGN KEY (`user_id`) REFERENCES `trivia_user_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION,
  ADD CONSTRAINT `scheduled_games_ibfk_2` FOREIGN KEY (`guild_id`) REFERENCES `trivia_guild_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION,
  ADD CONSTRAINT `scheduled_games_ibfk_3` FOREIGN KEY (`channel_id`) REFERENCES `trivia_channel_cache` (`id`) ON DELETE NO ACTION ON UPDATE NO ACTION;

ALTER TABLE `scores`
  ADD CONSTRAINT `scores_ibfk_1` FOREIGN KEY (`guild_id`) REFERENCES `trivia_guild_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION;

ALTER TABLE `start_log`
  ADD CONSTRAINT `start_log_ibfk_1` FOREIGN KEY (`user_id`) REFERENCES `trivia_user_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION;

ALTER TABLE `streaks`
  ADD CONSTRAINT `fk_guild_id` FOREIGN KEY (`guild_id`) REFERENCES `trivia_guild_cache` (`snowflake_id`),
  ADD CONSTRAINT `fk_user_id` FOREIGN KEY (`nick`) REFERENCES `trivia_user_cache` (`snowflake_id`);

ALTER TABLE `teams`
  ADD CONSTRAINT `teams_ibfk_1` FOREIGN KEY (`owner_id`) REFERENCES `trivia_user_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION;

ALTER TABLE `team_membership`
  ADD CONSTRAINT `fk_team` FOREIGN KEY (`team`) REFERENCES `teams` (`name`),
  ADD CONSTRAINT `team_membership_ibfk_1` FOREIGN KEY (`nick`) REFERENCES `trivia_user_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION;

ALTER TABLE `trivia_access`
  ADD CONSTRAINT `trivia_access_ibfk_1` FOREIGN KEY (`user_id`) REFERENCES `trivia_user_cache` (`snowflake_id`);

ALTER TABLE `trivia_channel_cache`
  ADD CONSTRAINT `trivia_channel_cache_ibfk_1` FOREIGN KEY (`parent_id`) REFERENCES `trivia_channel_cache` (`id`) ON DELETE NO ACTION ON UPDATE NO ACTION;

ALTER TABLE `trivia_guild_membership`
  ADD CONSTRAINT `fk_guild` FOREIGN KEY (`guild_id`) REFERENCES `trivia_guild_cache` (`snowflake_id`),
  ADD CONSTRAINT `fk_user` FOREIGN KEY (`user_id`) REFERENCES `trivia_user_cache` (`snowflake_id`);

ALTER TABLE `trivia_role_cache`
  ADD CONSTRAINT `trivia_role_cache_ibfk_1` FOREIGN KEY (`guild_id`) REFERENCES `trivia_guild_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION;

ALTER TABLE `warnings`
  ADD CONSTRAINT `warnings_ibfk_1` FOREIGN KEY (`insane_id`) REFERENCES `insane` (`id`),
  ADD CONSTRAINT `warnings_ibfk_2` FOREIGN KEY (`question_id`) REFERENCES `questions` (`id`),
  ADD CONSTRAINT `warnings_ibfk_3` FOREIGN KEY (`user_id`) REFERENCES `trivia_user_cache` (`snowflake_id`) ON DELETE NO ACTION ON UPDATE NO ACTION,
  ADD CONSTRAINT `warnings_ibfk_4` FOREIGN KEY (`warned_by_id`) REFERENCES `trivia_user_cache` (`snowflake_id`);

SET FOREIGN_KEY_CHECKS=1;
COMMIT;

/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;
/*!40101 SET CHARACTER_SET_RESULTS=@OLD_CHARACTER_SET_RESULTS */;
/*!40101 SET COLLATION_CONNECTION=@OLD_COLLATION_CONNECTION */;

