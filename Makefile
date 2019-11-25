# name of your application
APPLICATION = saul_coap

# If no BOARD is found in the environment, use this default:
BOARD ?= native

# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(CURDIR)/RIOT

BOARD_INSUFFICIENT_MEMORY := arduino-duemilanove arduino-leonardo arduino-nano \
                             arduino-uno

# Include packages that pull up and auto-init the link layer.
# NOTE: 6LoWPAN will be included if IEEE802.15.4 devices are present
USEMODULE += gnrc_netdev_default
USEMODULE += auto_init_gnrc_netif
# Specify the mandatory networking modules
USEMODULE += gnrc_ipv6_default
USEMODULE += gcoap
# Additional networking modules that can be dropped if not needed
USEMODULE += gnrc_icmpv6_echo
USEMODULE += gnrc_icmpv6_error

# Specify the mandatory networking modules for IPv6 routing
USEMODULE += gnrc_ipv6_router_default

# Required by gcoap example
USEMODULE += od
USEMODULE += fmt

# we want to use SAUL:
USEMODULE += saul_default
# include the shell:
USEMODULE += shell
USEMODULE += shell_commands
# additional modules for debugging:
USEMODULE += ps

USEMODULE += netstats_l2

CFLAGS += -DGNRC_IPV6_NIB_CONF_SLAAC=1


#choose tiny dtls stack 
#if errors, disable
USEMODULE += tinydtls_sock_dtls
CFLAGS += -DDTLS_PSK
# when using plain CoAP. Uncomment the next line to fix this.
CFLAGS += -DGCOAP_PDU_BUF_SIZE=256

# Comment this out to disable code in RIOT that does safety checking
# which is not needed in a production environment but helps in the
# development process:
DEVELHELP ?= 1

# Change this to 0 show compiler invocation lines by default:
QUIET ?= 1

include $(RIOTBASE)/Makefile.include
