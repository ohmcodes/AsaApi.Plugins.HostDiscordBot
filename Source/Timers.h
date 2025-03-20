
#if 0
void TimerCallback()
{


	HostDiscordBot::counter++;
}
#endif

#if 0
void SetTimers(bool addTmr = true)
{
	if (addTmr)
	{
		AsaApi::GetCommands().AddOnTimerCallback("HostDiscordBotTimerTick", &TimerCallback);
	}
	else
	{
		AsaApi::GetCommands().RemoveOnTimerCallback("HostDiscordBotTimerTick");
	}
}
#endif