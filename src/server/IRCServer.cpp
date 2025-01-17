#include <stdexcept>
#include <unistd.h>
#include <sys/socket.h>
#include <climits>
#include <cerrno>
#include <iostream>
#include <algorithm>
#include <map>
#include <netinet/in.h>
#include <fcntl.h>
#include "server/IRCServer.hpp"
#include "server/IRCClient.hpp"
#include "utils/Logger.hpp"
#include "utils/FuckCast.hpp"
#include "utils/nullptr.hpp"

bool IRCServer::mCmdHandlersInit = false;
handler_map_type IRCServer::mCmdHandlers;
IRCServer *IRCServer::lastInstance = nilptr;

void IRCServer::initCmdHandlers() {
	mCmdHandlers["QUIT"] = &IRCServer::handleQUIT;
	mCmdHandlers["NICK"] = &IRCServer::handleNICK;
	mCmdHandlers["PASS"] = &IRCServer::handlePASS;
	mCmdHandlers["USER"] = &IRCServer::handleUSER;
	mCmdHandlers["PING"] = &IRCServer::handlePING;
	mCmdHandlers["JOIN"] = &IRCServer::handleJOIN;
	mCmdHandlers["PART"] = &IRCServer::handlePART;
	mCmdHandlers["PRIVMSG"] = &IRCServer::handlePRIVMSG;
	mCmdHandlers["CAP"] = &IRCServer::handleCAP;
	mCmdHandlers["AP"] = &IRCServer::handleCAP;
	mCmdHandlers["KICK"] = &IRCServer::handleKICK;
	mCmdHandlers["INVITE"] = &IRCServer::handleINVITE;
	mCmdHandlers["TOPIC"] = &IRCServer::handleTOPIC;
	mCmdHandlers["MODE"] = &IRCServer::handleMODE;
	mCmdHandlers["WHO"] = &IRCServer::handleWHO;
	mCmdHandlersInit = true;
}

IRCServer::IRCServer(unsigned short port, const std::string &password, const std::string &ip)
	: mPort(port), mPassword(password), mSocketFd(0), mIsBound(false),
	  mIsListening(false), mShouldStop(false), mHost(ip), mChannelManager(this), mBotNick("bottich") {
	if (!mCmdHandlersInit)
		initCmdHandlers();

	mHost = ip;
	mCmdBase.mPrefix.mHostname = mHost;
	lastInstance = this;
}

IRCServer::~IRCServer() {
	this->internalStop();
}

void IRCServer::bind() {
	mSocketFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (mSocketFd < 0)
		throw std::runtime_error("Socket creation failed");

	if (fcntl(mSocketFd, F_SETFL, O_NONBLOCK) == -1)
		throw std::runtime_error("fcntl set failed");

	static const int state = 1;
	if (setsockopt(mSocketFd, SOL_SOCKET, SO_REUSEADDR, &state, sizeof(state)) < 0)
		throw std::runtime_error("Could not set socket options");

	sockaddr_in socketAddr = {};
	socketAddr.sin_family = AF_INET;
	socketAddr.sin_port = htons(mPort);
	socketAddr.sin_addr.s_addr = INADDR_ANY;
	int bindResult = ::bind(mSocketFd, reinterpret_cast<struct sockaddr *>(&socketAddr), sizeof(socketAddr));
	if (bindResult < 0) {
		close(mSocketFd);
		throw std::runtime_error("Socket binding failed. Is there another instance of the server running?");
	}

	mIsBound = true;
}

void IRCServer::listen() {
	if (!mIsBound)
		this->bind();
	if (::listen(mSocketFd, INT_MAX) < 0)
		throw std::runtime_error("Could not make socket listen");
	mIsListening = true;
}

int IRCServer::loop() {
	if (!mIsListening)
		this->listen();
	while (!mShouldStop) {
		this->pollClients();
		this->acceptNewClients();
	}
	return 0;
}

void IRCServer::stop() {
	mShouldStop = true;
}

void IRCServer::internalStop() {
	for (size_t i = 0; i < mClients.size(); i++)
		delete mClients[i];
	mClients.clear();
	if (!mShouldStop && (!mIsBound || !mIsListening))
		return;
	mShouldStop = true;
	close(mSocketFd);
	mIsListening = false;
	mIsBound = false;
	mSocketFd = 0;
}

void IRCServer::acceptNewClients() {
	sockaddr_in clientAddr;
	int clientSocket;
	socklen_t clientAddrLen;

	while (true) {
		clientAddrLen = sizeof(clientAddr);
		clientSocket = accept(mSocketFd, reinterpret_cast<struct sockaddr *>(&clientAddr), &clientAddrLen);
		if (clientSocket == -1 && errno == EWOULDBLOCK)
			return;
		LOG("Received client connection");
		IRCClient *client = new IRCClient(this, clientSocket);
		if (!client->isValid()) {
			delete client;
			throw std::runtime_error(
				std::string("Unable to accept connection."));
		}
		mClients.push_back(client);
	}
}

bool IRCServer::receiveData(IRCClient *client, std::string *buffer) {
	char preBuf[MSG_BUFFER_SIZE + 1] = { 0 };
	ssize_t received;

	received = recv(client->getSocketFd(), preBuf, MSG_BUFFER_SIZE + 1, 0);
	if (received == -1)
		throw std::runtime_error("An error occurred while trying to receive the sockets message.");
	if (!received)
		return false;
	*std::remove(preBuf, preBuf + received, '\r') = 0;
	buffer->append(preBuf);
	return true;
}

bool IRCServer::handle(IRCClient *client) {
	std::string &buf = client->mBuffer;
	short revents = client->poll();
	if (revents & POLLHUP || revents & POLLERR || revents & POLLNVAL)
		return false;
	if (!(revents & POLLIN))
		return true;
	if (!receiveData(client, &buf))
		return false;

	while (!buf.empty()) {
		size_t end = buf.find('\n');
		if (end == std::string::npos)
			break;
		IRCCommand cmd(buf.substr(0, end + 1));
		LOG(CYAN("[IN] ") << CYAN((std::string) cmd));
		buf = buf.substr(end + 1);
		if (!cmd.isValid())
			continue;

		if (cmd.mCommand.mName != "CAP" && cmd.mCommand.mName != "PASS" && (!client->hasAccess() ||
											 (!client->isRegistered() && cmd.mCommand.mName != "NICK" &&
											  cmd.mCommand.mName != "USER")))
			return false;

		handler_map_type::iterator cmdIt = mCmdHandlers.find(cmd.mCommand.mName);
		if (cmdIt == mCmdHandlers.end()) {
			LOG(YELLOW("[IN] === NOT IMPLEMENTED ==="));
			LOG(YELLOW("[IN] ") << YELLOW(cmd.mCommand.mName));
			LOG(YELLOW("[IN] ===      ====       ==="));
			continue;
		}
		if (!(this->*(cmdIt->second))(client, cmd))
			return false;
	}
	return true;
}

void IRCServer::pollClients() {
	for (size_t i = 0; i < mClients.size(); i++) {
		bool keepConnection = this->handle(mClients[i]);
		mClients[i]->mIsOpen = true;
		if (keepConnection)
			continue;
		mClients[i]->flushResponse();
		mChannelManager.partFromAll(mClients[i], mClients[i]->mQuitReason);
		mClients[i]->mIsOpen = false;
		LOG("[INFO] Client disconnected");
		delete mClients[i];
		mClients.erase(std::remove(mClients.begin(), mClients.end(), mClients[i]), mClients.end());
		i--;
	}
}

IRCCommand IRCServer::getResponseBase() {
	if (!lastInstance)
		throw std::runtime_error("No instance of IRCServer available");
	return lastInstance->mCmdBase;
}

const std::string &IRCServer::getHostname() {
	return mHost;
}

const std::vector<const IRCClient *> &IRCServer::getClients() const {
	return std::fuck_cast<const std::vector<const IRCClient *> >(mClients);
}

const std::string &IRCServer::getPassword() {
	return mPassword;
}
