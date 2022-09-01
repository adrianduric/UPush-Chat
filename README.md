# UPush-Chat
A chat service project from a former assignment, titled UPush.

Two main structs are implemented: client (klient) and message (melding). A client stores information used when communicating with other clients,
as well as saving sent messages to them. A message holds the text itself, which is sent as a packet, along with other variables being used for various
functions in the program. Both structs are saved in linked lists; the client linked list is globally accessible within each client and server process.
Each client also has its own message linked list. There are functions to insert and remove from both types of linked lists.

The server (upush_server.c) is structured so it constantly listens for messages from clients. Everything it does is to read incoming messages, verify
the format and handle the message afterwards. REG messages register the client sending the message in the server's own client linked list, while
LOOKUP messages search through the same list for already registered clients, and send an appropriate response back to the client.

The client's (upush_client.c) structure is the most complex. It too has a linked list of clients, and these have their saved messages stored with them.
Nevertheless, the main event loop is more complicated, since timeouts have to be handled.

When the client registers input from the keyboard, it reads off the desired action from the user, and executes it, given correct input. The most usual
and most complex occurence handling, is that of text messages to other structs. This is implemented so that the chain of events is as similar as
possible for an already saved client, and a not saved one. To do this, I have implemented that lookup messages can be connected to text messages.
If a message doesn't reach its destination, it's flagged as inactive, and a lookup message is generated, connected to the text message and sent to the
server. If this gives a successful lookup, the text message is reactivated with new attempts, but a flag is set so it will no longer get new lookup
messages to the server if it fails again.

When the client receives a message on its socket, it reads off the message type and checks whether it is a message or an ACK for a formerly sent
message. If it was a text, it verifies the format, prints the message, and sends an ACK back. If it was an ACK, the client goes through all of its
sent messages and tracks which one the ACK corresponds to. This saved message is then deleted, and otherwise exceptions are handled according to
protocol (e.g. ACK OK or ACK NOT FOUND).

Every 10th millisecond, the client iterates through all of its saved clients and their saved messages. If the message has passed its assigned
timeout time (an attribute saved in message structs), it is attempted resent, or is handled in another way if timeout attempts are already exhausted.
Messages to the server are also checked; the server is saved as a client struct at client process startup, spesifically with the intention of
handling messages to and from it and other clients in the same way.
