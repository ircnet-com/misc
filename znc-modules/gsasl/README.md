# gsasl
gsasl is a SASL module based on libgsasl. The following mechanisms are supported:
* PLAIN
* SCRAM-SHA-1
* SCRAM-SHA-256

Basically all mechanisms supported by libgsasl can be used. Edit the source code of the module to enable more of them.

## Installation
### libgsasl
Install libgsasl with your package management system or from source code.

If you want to use SCRAM-SHA-256, you need at least version 1.10.0.

### ZNC module
This module was tested on ZNC 1.8.2.

Compile the module:
```
LIBS=-lgsasl znc-buildmod gsasl.cpp
```

Copy ```gsasl.so``` to ``~/.znc/modules/`` or ```/usr/local/lib/znc/```

Load the module:
```
/msg *status loadmod gsasl
```

## Configuration
In this example SCRAM-SHA-256 will be used. If the authentication does not succeed, the bouncer will disconnect. Verbosity is enabled for demonstration purposes.
```
/msg *gsasl mechanism SCRAM-SHA-256
/msg *gsasl auth patrick password123
/msg *gsasl requireauth yes
/msg *gsasl verbose yes
```

```
<*status> Connecting to localhost...
<*gsasl> Sent: AUTHENTICATE SCRAM-SHA-256
<*gsasl> Received: +
<*gsasl> Sent: AUTHENTICATE biwsbj11c2VyMSxyPW8wUm5ZWG1iWDFKaFFQWXpwKzVTZjlsbA==
<*gsasl> Received: cj1vMFJuWVhtYlgxSmhRUFl6cCs1U2Y5bGw2MDgwMmZlZS1kZDY2LTRlNDQtOWE2My03NjUxNDNmYjNjOWIscz0rUEFDQjZ5SU5iOVVOd1lzSzlOcUxCL05BUUdYdnhJcSxpPTQxNjM=
<*gsasl> Sent: AUTHENTICATE Yz1iaXdzLHI9bzBSbllYbWJYMUpoUVBZenArNVNmOWxsNjA4MDJmZWUtZGQ2Ni00ZTQ0LTlhNjMtNzY1MTQzZmIzYzliLHA9QWt6dTZFUnp5VjRTWTRIUElKVkdQTlFtU29oWGJHbWl5S2dpZ3FFWjM4az0=
<*gsasl> Received: dj0xOExUMitrWEpxYjF2UktlTmJHUStNM1JKaTBWWThPMStZcEpiUTlQQS9zPQ==
<*gsasl> SASL authentication succeeded.
```

## Contact
\#ircnet.com on IRCnet