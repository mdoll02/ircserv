#include "server/IRCServer.hpp"

void IRCServer::handleKICK(IRCClient *client, const IRCCommand &cmd) {
	if (cmd.mParams.size() < 2)
		return;
	const std::string &channel = cmd.mParams[0];
	const std::string &targetClientNick = cmd.mParams[1];
	const std::string &kickMessage = cmd.mEnd;
	mChannelManager.kick(client, channel, targetClientNick, kickMessage);
	// TODO: WeeChat outputs "sender has kicked" and that's it. needs to be checked.
}