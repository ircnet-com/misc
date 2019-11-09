/*
 * Topic keeper v1.0
 *
 * This module resets the topic after a configured time.
 *
 * <patrick> set
 * <*topickeeper> +===================+==================+
 * <*topickeeper> | Variable          | Value            |
 * <*topickeeper> +===================+==================+
 * <*topickeeper> | channels          | #irc #ircnet.com |
 * <*topickeeper> +-------------------+------------------+
 * <*topickeeper> | max_delay         | 120              |
 * <*topickeeper> +-------------------+------------------+
 * <*topickeeper> | min_delay         | 1                |
 * <*topickeeper> +-------------------+------------------+
 * <*topickeeper> | topic_update_time | 86400            |
 * <*topickeeper> +===================+==================+
 *
 * The topic will be refreshed after "topic_update_time" seconds. To avoid multiple bouncers to set the topic at the
 * same time, a random delay between "min_delay" and "max_delay" will be is applied.
 *
 *
 * Send bug reports to patrick@ircnet.com or join #irc on IRCnet.
 */

#include <znc/IRCNetwork.h>
#include <znc/Client.h>
#include <znc/Chan.h>
#include <znc/Modules.h>

using std::vector;

class CTopicJob : public CTimer {
protected:
    void RunJob() override;

public:
    CTopicJob(CModule *pModule, unsigned int uInterval, unsigned int uCycles,
              const CString &sLabel,
              const CString &sDescription) : CTimer(pModule, uInterval, uCycles, sLabel, sDescription) {}

    ~CTopicJob() override {}
};

class CUpdateTopicJob : public CTimer {
private:
    time_t jobCreationTime;
    CString channelName;
    CString nvTopicPrefix;
    std::map<CString, time_t> *lastUpdateAttempt;

protected:
    void RunJob() override {
        CChan *channel = GetModule()->GetNetwork()->FindChan(channelName);

        if (!channel || !channel->IsOn()) {
            // Left channel in the mean while
            return;
        }

        if (channel->GetTopicDate() > jobCreationTime) {
            // Topic has been changed in the mean while
            return;
        }

        if (!channel->HasPerm(CChan::Op) && channel->GetModeString().find('t') != std::string::npos) {
            // Lost permission to change the topic
            return;
        }

        if (!channel->GetTopic().empty()) {
            // Topic is set. Just set the same topic again.
            GetModule()->PutIRC("TOPIC " + channel->GetName() + " :" + channel->GetTopic());
            lastUpdateAttempt->insert(std::pair<CString, time_t>(channel->GetName(), time(nullptr)));
        }
        else {
            // The bouncer does not see any topic. Try to find saved topic.
            CString savedTopic = GetModule()->GetNV(nvTopicPrefix + channel->GetName().AsLower());

            if (!savedTopic.empty()) {
                GetModule()->PutIRC("TOPIC " + channel->GetName() + " :" + savedTopic);
                lastUpdateAttempt->insert(std::pair<CString, time_t>(channel->GetName(), time(nullptr)));
            }
        }
    };

public:
    CUpdateTopicJob(CModule *pModule, CString _channelName, const CString _nvTopicPrefix,
                    std::map<CString, time_t> *_lastUpdateAttempt, unsigned int uInterval, unsigned int uCycles,
                    const CString &sLabel,
                    const CString &sDescription) : CTimer(pModule, uInterval, uCycles, sLabel, sDescription) {
        jobCreationTime = time(nullptr);
        channelName = _channelName;
        nvTopicPrefix = _nvTopicPrefix;
        lastUpdateAttempt = _lastUpdateAttempt;
    }

    ~CUpdateTopicJob() override {}
};

class CTopicKeeperMod : public CModule {
private:
    const CString nvConfigPrefix = "cfg::";
    const CString nvTopicPrefix = "topic::";
    const CString nvChannels = nvConfigPrefix + "channels";
    const CString nvTopicUpdateTime = nvConfigPrefix + "topic_update_time";
    const CString nvMinDelay = nvConfigPrefix + "min_delay";
    const CString nvNaxDelay = nvConfigPrefix + "max_delay";
    std::map<CString, time_t> lastUpdateAttempt;

public:
    MODCONSTRUCTOR(CTopicKeeperMod) {}

    bool OnLoad(const CString &sArgs, CString &sMessage) override {
        // Set default values

        if (GetNV(nvChannels).empty()) {
            SetNV(nvChannels, "#irc #ircnet.com");
        }

        if (GetNV(nvTopicUpdateTime).empty()) {
            SetNV(nvTopicUpdateTime, "86400");
        }

        if (GetNV(nvMinDelay).empty()) {
            SetNV(nvMinDelay, "1");
        }

        if (GetNV(nvNaxDelay).empty()) {
            SetNV(nvNaxDelay, "120");
        }

        // Remove topics of channels where the bouncer is not joined anymore
        removeObsoleteTopics();

        // Save all topics
        saveAllTopics();

        // Add a timer which checks if topics must be updated
        AddTimer(new CTopicJob(this, 10, 0, "Topic Check", "Checks if topics must be updated."));

        return true;
    }

    ~CTopicKeeperMod() override {
        PutModule("Topic Keeper is unloading");
    }

    EModRet OnRaw(CString &sLine) override {
        // Topic reply after JOIN
        if (sLine.Token(1) == "332") {
            CString topic = sLine.Token(4, true).substr(1);
            SetNV(nvTopicPrefix + sLine.Token(3).AsLower(), topic);
        }

        return CONTINUE;
    }

    EModRet OnTopic(CNick &Nick, CChan &Channel, CString &sTopic) override {
        SetNV(nvTopicPrefix + Channel.GetName().AsLower(), sTopic);
        return CONTINUE;
    }

    void OnModCommand(const CString &sCommand) override {
        const CString tableHeaderVariable = "Variable";
        const CString tableHeaderValue = "Value";
        const CString errorNumberExpected = "Value must be a number";
        const CString errorUnknownVariable = "Unknown variable";

        CString command = sCommand.Token(0).AsUpper();

        if (command == "SET") {
            CTable table;
            CString var = sCommand.Token(1).AsLower();
            CString val = sCommand.Token(2, true);
            val.Trim();

            table.AddColumn(tableHeaderVariable);
            table.AddColumn(tableHeaderValue);

            if (var.empty()) {
                // Show all variables
                for (MCString::iterator it = BeginNV(); it != EndNV(); it++) {
                    if (it->first.StartsWith(nvConfigPrefix)) {
                        table.AddRow();
                        table.SetCell(tableHeaderVariable, it->first.substr(nvConfigPrefix.length()));
                        table.SetCell(tableHeaderValue, it->second);
                    }
                }

                PutModule(table);
            }

            else {
                // Show or change a variable
                CString internalVariable = nvConfigPrefix + var;
                bool ok = true;

                if (internalVariable == nvChannels) {
                    if (!val.empty()) {
                        SetNV(nvChannels, val);
                    }
                }

                else if (internalVariable == nvTopicUpdateTime) {
                    if (!val.empty()) {
                        if (isNumber(val)) {
                            SetNV(nvTopicUpdateTime, val);
                        }
                        else {
                            ok = false;
                            PutModule(errorNumberExpected);
                        }
                    }
                }

                else if (internalVariable == nvMinDelay) {
                    if (!val.empty()) {
                        if (isNumber(val)) {
                            SetNV(nvMinDelay, val);
                        }
                        else {
                            ok = false;
                            PutModule(errorNumberExpected);
                        }
                    }
                }

                else if (internalVariable == nvNaxDelay) {
                    if (!val.empty()) {
                        if (isNumber(val)) {
                            SetNV(nvNaxDelay, val);
                        }
                        else {
                            ok = false;
                            PutModule(errorNumberExpected);
                        }
                    }
                }

                else {
                    // Unknown variable
                    ok = false;
                    PutModule(errorUnknownVariable);
                }

                if (ok) {
                    table.AddRow();
                    table.SetCell(tableHeaderVariable, var);
                    table.SetCell(tableHeaderValue, GetNV(internalVariable));
                    PutModule(table);
                }
            }
        }

        else if (command == "DEBUG") {
            CTable table;

            table.AddColumn(tableHeaderVariable);
            table.AddColumn(tableHeaderValue);

            for (MCString::iterator it = BeginNV(); it != EndNV(); it++) {
                table.AddRow();
                table.SetCell(tableHeaderVariable, it->first);
                table.SetCell(tableHeaderValue, it->second);
            }

            PutModule(table);
        }
    }

    /**
     * Checks if this module is active for the given channel.
     *
     * @param channelName A channel name
     * @return true if the module is active for this channel, otherwise false
     */
    bool isActiveChannel(CString channelName) {
        channelName = channelName.AsLower();
        CString channelList = GetNV(nvChannels);
        CString arg;
        int i = 0;

        arg = channelList.Token(i++).AsLower();

        while (!arg.empty()) {
            if (channelName == arg) {
                return true;
            }

            arg = channelList.Token(i++).AsLower();
        }

        return false;
    }

    /**
     * Iterates over channels and checks if a topic must be refreshed.
     */
    void checkChannels() {
        const std::vector<CChan *> &channels = GetNetwork()->GetChans();
        const int topicUpdateTime = GetNV(nvTopicUpdateTime).ToInt();
        const time_t now = time(nullptr);

        for (CChan *channel: channels) {
            if (!isActiveChannel(channel->GetName())) {
                continue;
            }

            if (!channel->IsOn()) {
                continue;
            }

            if (!channel->HasPerm(CChan::Op) && channel->GetModeString().find('t') != std::string::npos) {
                continue;
            }

            const std::map<CString, time_t>::iterator record = lastUpdateAttempt.find(channel->GetName());

            if (record != lastUpdateAttempt.end()) {
                // Check if the bouncer has just sent TOPIC to avoid repetitions during lag
                if (now - record->second < 120) {
                    continue;
                }
            }

            if (now - channel->GetTopicDate() > topicUpdateTime) {
                // Topic update is due
                CString timerName = "Topic reset for " + channel->GetName().AsLower();

                if (FindTimer(timerName)) {
                    // There is already a job for updating this topic
                    continue;
                }

                bool ok = false;
                int minDelay = GetNV(nvMinDelay).ToInt();
                int maxDelay = GetNV(nvNaxDelay).ToInt();

                if (!channel->GetTopic().empty()) {
                    ok = true;
                }

                else {
                    // The bouncer does not see any topic. Try to find a saved topic.
                    CString savedTopic = GetNV(nvTopicPrefix + channel->GetName().AsLower());

                    if (!savedTopic.empty()) {
                        ok = true;
                    }
                }

                if (ok) {
                    srand(time(NULL));
                    int delay = rand() % (maxDelay - minDelay + 1) + minDelay;
                    CUpdateTopicJob *job = new CUpdateTopicJob(this, channel->GetName(), nvTopicPrefix, &lastUpdateAttempt,
                                                               delay, 1, timerName, "Updates the topic after a delay");
                    AddTimer(job);
                }
            }
        }
    }

    void saveAllTopics() {
        const std::vector<CChan *> &channels = GetNetwork()->GetChans();

        for (CChan *channel: channels) {
            if (!channel->IsOn()) {
                continue;
            }

            if(!channel->GetTopic().empty()) {
                SetNV(nvTopicPrefix + channel->GetName().AsLower(), channel->GetTopic());
            }
        }
    }

    /**
     * Removes obsolete topics.
     */
    void removeObsoleteTopics() {
        for (MCString::iterator it = BeginNV(); it != EndNV(); it++) {
            if (it->first.StartsWith(nvTopicPrefix)) {
                CString channelName = CString(it->first.substr(nvTopicPrefix.length()));
                channelName = channelName.AsLower();

                const std::vector<CChan *> &channels = GetNetwork()->GetChans();

                bool found = false;

                for (CChan *channel: channels) {
                    if (channel->GetName().AsLower() == channelName) {
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    DelNV(it++);
                    // PutModule("Removed saved topic for channel " + channelName);
                }
                else {
                    ++it;
                }
            }
        }
    }

    bool isNumber(const std::string &s) {
        std::string::const_iterator it = s.begin();

        while (it != s.end() && std::isdigit(*it)) {
            ++it;
        }

        return !s.empty() && it == s.end();
    }
};

void CTopicJob::RunJob() {
    CTopicKeeperMod *module = (CTopicKeeperMod *) GetModule();
    module->checkChannels();
}

template<>
void TModInfo<CTopicKeeperMod>(CModInfo &Info) {
    Info.SetHasArgs(false);
}

MODULEDEFS(CTopicKeeperMod, "Refreshes the topic.")
