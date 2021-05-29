/*
 * Copyright (C) 2004-2021 ZNC, see the NOTICE file for details.
 * Copyright (C) 2021 IRCnet.com team
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <znc/IRCNetwork.h>
#include <znc/IRCSock.h>
#include <algorithm>
#include <gsasl.h>

#define NV_MECHANISM "sasl_mechanism"
#define NV_REQUIRE_AUTH "sasl_require_auth"

class CSASLMod : public CModule {
    const char *supportedMechanisms[3] = {
            "PLAIN", // Not recommended on IRCnet
            //"EXTERNAL", // Not supported on IRCnet
            "SCRAM-SHA-1", // For old gsasl versions that do not supported SCRAM-SHA-256
            "SCRAM-SHA-256", // the preferred mechanism ;-)
    };

public:
    MODCONSTRUCTOR(CSASLMod) {
            int rc;
            supportedMechanisms_size = sizeof(supportedMechanisms) / sizeof(supportedMechanisms[0]);

            // Initialize gsasl library
            if ((rc = gsasl_init (&ctx)) != GSASL_OK)
            {
                PutModule(t_f("Cannot initialize libgsasl: {1}")(gsasl_strerror(rc)));
                return;
            }

            // Commands
            AddCommand("Help", t_d(""), t_d("Generate this output"),
            [=](const CString& sLine) { HelpCommand(sLine); });

            AddCommand("Auth", t_d("[<username> [<password>]]"),
            t_d("Set username and password for the mechanisms that need "
            "them. Password is optional. Without parameters, "
            "returns information about current settings."),
            [=](const CString& sLine) { AuthCommand(sLine); });

            AddCommand("Mechanism", t_d("[mechanism]"),
            t_d("Set the mechanism"),
            [=](const CString& sLine) { MechanismCommand(sLine); });

            AddCommand("RequireAuth", t_d("[yes|no]"),
            t_d("Don't connect unless SASL authentication succeeds"),
            [=](const CString& sLine) { RequireAuthCommand(sLine); });

            AddCommand("Verbose", "yes|no", "Set verbosity level, useful to debug",
            [&](const CString& sLine) {
                verbose = sLine.Token(1, true).ToBool();
                PutModule("Verbose: " + CString(verbose));
            });

            authenticated = false;
    }

    ~CSASLMod() override {
        gsasl_done(ctx);
    }

    void HelpCommand(const CString &sLine) {
        HandleHelpCommand(sLine);

        CTable Mechanisms;
        Mechanisms.AddColumn(t_s("Mechanism"));
        Mechanisms.AddColumn(t_s("Description"));
        Mechanisms.SetStyle(CTable::ListStyle);

        for (int i = 0; i < supportedMechanisms_size; i++) {
            Mechanisms.AddRow();
            Mechanisms.SetCell(t_s("Mechanism"), supportedMechanisms[i]);

            if (!gsasl_client_support_p(ctx, supportedMechanisms[i])) {
                Mechanisms.SetCell(t_s("Description"), "not supported by your gsasl version");
                continue;
            }
        }

        PutModule("");
        PutModule(t_s("The following mechanisms are available:"));
        PutModule(Mechanisms);
    }

    void AuthCommand(const CString &sLine) {
        if (sLine.Token(1).empty()) {
            CString sUsername = GetNV("username");
            CString sPassword = GetNV("password");

            if (sUsername.empty()) {
                PutModule(t_s("Username is currently not set"));
            }
            else {
                PutModule(t_f("Username is currently set to '{1}'")(sUsername));
            }
            if (sPassword.empty()) {
                PutModule(t_s("Password was not supplied"));
            }
            else {
                PutModule(t_s("Password was supplied"));
            }
            return;
        }

        SetNV("username", sLine.Token(1));
        SetNV("password", sLine.Token(2));

        PutModule(t_f("Username has been set to '{1}'")(GetNV("username")));
        PutModule(t_f("Password has been set to '{1}'")(GetNV("password")));
    }

    void MechanismCommand(const CString &sLine) {
        CString sMechanism = sLine.Token(1, true).AsUpper();

        if (!sMechanism.empty()) {
            if (!SupportsMechanism(sMechanism)) {
                PutModule(t_f("Unknown mechanism: {1}")(sMechanism));
            }
            else {
                // Check if gsasl supports the mechanism
                if (!gsasl_client_support_p(ctx, sMechanism.c_str())) {
                    PutModule(t_f("Your version of gsasl does not support {1}. Please upgrade!")(sMechanism));
                    return;
                }

                SetNV(NV_MECHANISM, sMechanism);
            }
        }

        PutModule(t_f("Current mechanism set: {1}")(GetMechanismsString()));
    }

    void RequireAuthCommand(const CString &sLine) {
        if (!sLine.Token(1).empty()) {
            SetNV(NV_REQUIRE_AUTH, sLine.Token(1));
        }

        if (GetNV(NV_REQUIRE_AUTH).ToBool()) {
            PutModule(t_s("We require SASL negotiation to connect"));
        }
        else {
            PutModule(t_s("We will connect even if SASL fails"));
        }
    }

    bool SupportsMechanism(const CString &sMechanism) {
        for (int i = 0; i < supportedMechanisms_size; i++) {
            if (!strcmp(sMechanism.c_str(), supportedMechanisms[i])) {
                return true;
            }
        }

        return false;
    }

    CString GetMechanismsString() const {
        return GetNV(NV_MECHANISM);
    }

    void CheckRequireAuth() {
        if (!authenticated && GetNV(NV_REQUIRE_AUTH).ToBool()) {
            GetNetwork()->SetIRCConnectEnabled(false);
            PutModule(t_s("Disabling network, we require authentication."));
            PutModule(t_s("Use 'RequireAuth no' to disable."));
        }
    }

    Gsasl_session *client(Gsasl *ctx) {
        Gsasl_session *session;
        const char *mech = GetNV(NV_MECHANISM).c_str();
        int rc;

        if ((rc = gsasl_client_start(ctx, mech, &session)) != GSASL_OK) {
            PutModule(t_f("Cannot initialize client: {1}")(gsasl_strerror(rc)));
            return NULL;
        }

        rc = gsasl_property_set(session, GSASL_AUTHID, GetNV("username").c_str());
        if (rc != GSASL_OK) {
            PutModule(t_f("Cannot set property {1}")(gsasl_strerror(rc)));
            return NULL;
        }

        rc = gsasl_property_set(session, GSASL_PASSWORD, GetNV("password").c_str());
        if (rc != GSASL_OK) {
            PutModule(t_f("Cannot set property {1}")(gsasl_strerror(rc)));
            return NULL;
        }

        return session;
    }

    void client_authenticate(Gsasl_session *session, const CString &sLine) {
        const char *buf = sLine.c_str();
        char *p;
        int rc = gsasl_step64(session, buf, &p);

        if (rc == GSASL_NEEDS_MORE || rc == GSASL_OK) {
            CString sLine;

            if(rc == GSASL_NEEDS_MORE) {
                sLine = "AUTHENTICATE " + CString(p);
            }
            else if(rc == GSASL_OK) {
                sLine = "AUTHENTICATE +";
            }

            PutIRC(sLine);

            if (verbose) {
                PutModule(t_f("Sent: {1}")(sLine));
            }

            gsasl_free(p);
        }

        if (rc != GSASL_NEEDS_MORE) {
            if (rc != GSASL_OK) {
                PutModule(t_f("Authentication error ({1}): {2}")(rc, gsasl_strerror(rc)));
                CheckRequireAuth();
                GetNetwork()->GetIRCSock()->ResumeCap();
            }

            gsasl_finish(session);
        }
    }

    /*
     * Received AUTHENTICATE.
     */
    void onAuthenticate(const CString &sLine) {
        if (verbose) {
            PutModule(t_f("Received: {1}")(sLine));
        }

        if (sLine.Equals("+") || session == NULL) {
            session = client(ctx);

            if (session == NULL) {
                CheckRequireAuth();
                GetNetwork()->GetIRCSock()->Quit();
                return;
            }
        }

        client_authenticate(session, !sLine.Equals("+") ? sLine : "");
    }

    bool OnServerCapAvailable(const CString &sCap) override {
        return sCap.Equals("sasl");
    }

    void OnServerCapResult(const CString &sCap, bool bSuccess) override {
        if (sCap.Equals("sasl")) {
            if (bSuccess) {
                CString mechanism = GetNV(NV_MECHANISM);

                if (mechanism.empty()) {
                    CheckRequireAuth();
                    return;
                }

                GetNetwork()->GetIRCSock()->PauseCap();
                CString sLine = "AUTHENTICATE " + mechanism;
                PutIRC(sLine);

                if (verbose) {
                    PutModule(t_f("Sent: {1}")(sLine));
                }
            }
            else {
                CheckRequireAuth();
            }
        }
    }

    EModRet OnRawMessage(CMessage &msg) override {
        if (msg.GetCommand().Equals("AUTHENTICATE")) {
            onAuthenticate(msg.GetParam(0));
            return HALT;
        }

        return CONTINUE;
    }

    EModRet OnNumericMessage(CNumericMessage &msg) override {
        CString mechanism = GetNV(NV_MECHANISM);

        if (msg.GetCode() == 903) {
            // RPL_SASLSUCCESS
            if (verbose) {
                PutModule(t_s("SASL authentication succeeded."));
            }

            GetNetwork()->GetIRCSock()->ResumeCap();
            authenticated = true;
        }
        else if (msg.GetCode() == 904 || msg.GetCode() == 905) {
            // ERR_SASLFAIL or  ERR_SASLTOOLONG
            PutModule(t_s("SASL authentication failed."));
            CheckRequireAuth();
            GetNetwork()->GetIRCSock()->ResumeCap();
        }
        else if (msg.GetCode() == 906) {
            // ERR_SASLABORTED
            PutModule(t_s("SASL authentication aborted."));
            CheckRequireAuth();
        }
        else if (msg.GetCode() == 907) {
            // ERR_SASLALREADY
            authenticated = true;
            GetNetwork()->GetIRCSock()->ResumeCap();
        }
        else {
            return CONTINUE;
        }
        return HALT;
    }

    void OnIRCConnected() override {
        /* Just in case something slipped through, perhaps the server doesn't
         * respond to our CAP negotiation. */
        CheckRequireAuth();
    }

    void OnIRCDisconnected() override { authenticated = false; }

private:
    Gsasl *ctx = NULL;
    Gsasl_session *session;
    bool authenticated;
    bool verbose = false;
    int supportedMechanisms_size;
};

template<> void TModInfo<CSASLMod>(CModInfo &Info) {
}

NETWORKMODULEDEFS(CSASLMod, t_s("SASL authentication based on libgsasl"))