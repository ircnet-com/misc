# ircd spoof patch
1. I:Line has been extended by an optional field for spoofed host names:

```I:<TARGET Host Addr>:<Password>:<TARGET Hosts NAME>:<Port>:<Class>:<Flags>:<Spoofed Host Name>:``` 

2. During registration on IRC, the real host name is replaced by the spoofed host name and the user name is prefixed by '_'.
3. The IP address is replaced by 255.255.255.255 in most cases. `beIR` etc will still match the real IP address.
4. I:Lines containing spoofed hostnames will not be listed in `STATS I`

## Note for operators
* you can (T)K-Line spoofed hosts directly

## Contact
patrick@ircnet.com, #irc on IRCnet
