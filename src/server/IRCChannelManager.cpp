#include "server/IRCChannelManager.hpp"

IRCChannel* IRCChannelManager::get(const std::string& channelName) {
	if (m_channels.find(channelName) == m_channels.end())
		return 0;
	return m_channels[channelName];
}

IRCChannel* IRCChannelManager::get_or_create(const std::string& channelName) {
	if (m_channels.find(channelName) == m_channels.end())
		m_channels[channelName] = new IRCChannel(channelName);
	return m_channels[channelName];
}

bool IRCChannelManager::join(const std::string& channelName, IRCClient* client) {
	return this->get_or_create(channelName)->join(client);
}

void IRCChannelManager::send(const std::string& channelName, const std::string& message) {
	IRCChannel* channel = this->get(channelName);
	if (!channel)
		return;
	channel->send(message);
}