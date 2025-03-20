#include <dpp/dpp.h>

#include <atomic>
#include <thread>

#include <cstdint>
#include <future>

nlohmann::json config = HostDiscordBot::config["DiscordBot"];


uint64_t ConvertGuildID(std::string guildIDStr)
{
	uint64_t guildID = 0;

	try
	{
		guildID = std::stoull(guildIDStr);
	}
	catch (const std::invalid_argument& e)
	{
		Log::GetLog()->error("Invalid Guild ID:{}  ERROR: {}", guildIDStr, e.what());
		return 0;
	}
	catch (const std::out_of_range& e)
	{
		Log::GetLog()->error("Guild ID out of range: {} ERROR: {}", guildIDStr, e.what());
		return 0;
	}

	return guildID;
}

dpp::command_option ConstructOptions(nlohmann::json options)
{
	dpp::command_option opt = dpp::command_option(dpp::co_string, options["Name"], options["Desc"], options["Required"].get<bool>());

	nlohmann::json choices = options["Choices"].get<nlohmann::json>();

	for (auto it = choices.begin(); it != choices.end(); ++it)
	{
		std::string key = it.key().c_str();
		std::string value = it.value().get<std::string>().c_str();

		opt.add_choice(dpp::command_option_choice(key, value));
	}

	return opt;
}

dpp::presence_status GetPresenceStatus(const std::string& status_str)
{
	static const std::unordered_map<std::string, dpp::presence_status> status_map = {
		{"ps_online", dpp::ps_online},
		{"ps_dnd", dpp::ps_dnd},
		{"ps_idle", dpp::ps_idle},
		{"ps_invisible", dpp::ps_invisible},
		{"ps_offline", dpp::ps_offline}
	};

	auto it = status_map.find(status_str);
	if (it != status_map.end())
	{
		return it->second;
	}
	else
	{
		return dpp::ps_offline;
	}
}

dpp::activity_type GetActivityType(const std::string& activity_str)
{
	static const std::unordered_map<std::string, dpp::activity_type> activity_map = {
		{"at_game", dpp::at_game},
		{"at_streaming", dpp::at_streaming},
		{"at_listening", dpp::at_listening},
		{"at_watching", dpp::at_watching},
		{"at_custom", dpp::at_custom},
		{"at_competing", dpp::at_competing}
	};

	auto it = activity_map.find(activity_str);
	if (it != activity_map.end())
	{
		return it->second;
	}
	else
	{
		return dpp::at_game;
	}
}

void SetPresence(dpp::cluster& discordBot)
{
	if (dpp::run_once<struct register_bot_commands>())
	{
		dpp::presence_status status = dpp::ps_offline;
		status = GetPresenceStatus(config["Presence"]["Status"]);

		dpp::activity activity;
		activity.name = config["Presence"]["ActivityName"];
		activity.type = GetActivityType(config["Presence"]["ActivityType"]);

		dpp::presence presence = dpp::presence(status, activity);

		discordBot.set_presence(presence);

		discordBot.start_timer([&discordBot, presence](const dpp::timer& timer)
			{
				discordBot.set_presence(presence);
			},
			config["Presence"]["Interval"].get<int>()
		
		);
	}
}

void DeleteAllCommands(dpp::cluster& discordBot, dpp::snowflake guild_id, std::promise<void>& delete_promise)
{
	discordBot.guild_commands_get(guild_id, [&discordBot, guild_id, &delete_promise](const dpp::confirmation_callback_t& callback)
		{
			if (callback.is_error())
			{
				Log::GetLog()->error("Failed to fetch guild commands: {}", callback.get_error().message);
				delete_promise.set_value();
				return;
			}

			dpp::slashcommand_map commands = callback.get<dpp::slashcommand_map>();
			if (commands.empty())
			{
				Log::GetLog()->info("No commands to delete in guild {}", guild_id);
				delete_promise.set_value();
				return;
			}

			// Perform delete commands
			discordBot.guild_bulk_command_delete(guild_id, [guild_id, &delete_promise](const dpp::confirmation_callback_t& del_callback)
				{
					if (del_callback.is_error())
					{
						Log::GetLog()->error("Failed to delete guild command Error: {}", del_callback.get_error().message);
					}
					else
					{
						Log::GetLog()->warn("All guild commands deleted");
					}

					delete_promise.set_value();
				}
			);
		}
	);
}

void RegisterCommands(dpp::cluster& discordBot, dpp::snowflake guild_id)
{
	if (dpp::run_once<struct register_bot_commands>())
	{
		std::vector<dpp::slashcommand> dpp_commands;

		nlohmann::json commands = config["DiscordBot"]["Commands"].get<nlohmann::json>();

		if (commands.is_null())
		{
			Log::GetLog()->warn("Command is null");
			return;
		}

		for (nlohmann::json cmd : commands)
		{
			std::string current_cmd = cmd.value("Name", "");
			std::string cuirrent_desc = cmd.value("Desc","");

			if (current_cmd.empty())
			{
				Log::GetLog()->error("Command Name is empty, skipping command registration");
				continue;
			}

			dpp::slashcommand slash_command(current_cmd, cuirrent_desc, discordBot.me.id);

			nlohmann::json options = cmd["Options"].get<nlohmann::json>();

			for (nlohmann::json opt : options)
			{
				const dpp::command_option op = ConstructOptions(opt);

				slash_command.add_option(op);
			}

			dpp_commands.push_back(slash_command);
		}

		if (!dpp_commands.empty())
		{
			discordBot.guild_bulk_command_create(dpp_commands, guild_id, [](const dpp::confirmation_callback_t& callback)
				{
					if (callback.is_error())
					{
						Log::GetLog()->error("Failed to register command. Error: {}", callback.get_error().message);
					}
					else
					{
						Log::GetLog()->info("All Commands are registered");
					}
				}
			);
		}
		else
		{
			Log::GetLog()->info("No valid commands to register");
		}
	}
}

void OnSlashCommand(dpp::cluster& discordBot)
{
	discordBot.on_slashcommand([&](const dpp::slashcommand_t& event)
		{
			nlohmann::json commands = config["Commands"];

			for (nlohmann::json cmd : commands)
			{
				if (event.command.get_command_name() == cmd["Name"])
				{
					nlohmann::json resp = cmd["Response"];

					if (resp.is_null()) continue;

					dpp::message msg;
					msg.content = resp["Message"];

					if (resp["Ephemeral"].get<bool>() == true)
					{
						msg.set_flags(dpp::m_ephemeral);
					}

					event.reply(msg);
				}
			}

			if (event.command.get_command_name() == "animal")
			{
				std::string user_id_str = std::to_string(event.command.get_issuing_user().id);

				std::string animal = std::get<std::string>(event.get_parameter("animals"));

				// check discord id in database
				// get eos id
				// get character from eos id
				// perform a in-game functions like giving item

				event.reply(dpp::message(std::string("Animal you've choose: " + animal + " USERID: " + user_id_str)).set_flags(dpp::m_ephemeral));
			}
		}
	);
}

void OnBotReady(dpp::cluster& discordBot)
{
	discordBot.on_ready([&](const dpp::ready_t& event)
		{
			Log::GetLog()->info("Discord bot is ready");

			dpp::snowflake guildID = ConvertGuildID(HostDiscordBot::config["DiscordBot"].value("GuildID", "0"));

			if (guildID == 0)
			{
				Log::GetLog()->error("Invalid GuildID, skipping command registration");
				return;
			}
			
			SetPresence(discordBot);

			// Delete all existing commands before registering new ones
			std::promise<void> delete_promise;
			std::future<void> delete_future = delete_promise.get_future();
			DeleteAllCommands(discordBot, guildID, delete_promise);
			delete_future.wait();

			RegisterCommands(discordBot, guildID);

			OnSlashCommand(discordBot);
		}
	);
}

void InitDiscordBot(bool isLoad = true)
{
	static dpp::cluster* discordBot = nullptr;
	static std::thread botThread;
	static std::atomic<bool> botRunning(false);

	if (isLoad)
	{
		Log::GetLog()->info("Load bot");

		if (!discordBot)
		{
			std::string BotToken = config.value("BotToken", "");

			if (BotToken.empty() || BotToken == "")
			{
				Log::GetLog()->error("Discord Bot Token is empty");
				return;
			}

			discordBot = new dpp::cluster(BotToken);

			OnBotReady(*discordBot);

			botRunning = true;
			botThread = std::thread([&]()
				{
					discordBot->start(dpp::st_wait);
				}
			);
		}
	}
	else
	{
		if (discordBot)
		{
			botRunning = false;
			if (botThread.joinable())
			{
				botThread.detach();
				delete discordBot;
				discordBot = nullptr;
				Log::GetLog()->warn("Discord bot stopped.");
			}
		}
	}
}