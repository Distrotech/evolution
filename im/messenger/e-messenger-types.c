#include "e-messenger-types.h"

const char *
e_messenger_signon_error_to_string (const EMessengerSignonError signon_error)
{
	switch (signon_error) {
	case E_MESSENGER_SIGNON_ERROR_NONE:
		return "SignonErrorNone";
	case E_MESSENGER_SIGNON_ERROR_NET_FAILURE:
		return "SignonErrorNetFailure";
	case E_MESSENGER_SIGNON_ERROR_INVALID_LOGIN:
		return "SignonErrorInvalidLogin";
	case E_MESSENGER_SIGNON_ERROR_PMS_FAILURE:
		return "SignonErrorPMSFailure";
	case E_MESSENGER_SIGNON_ERROR_DEFUNCT_CONNECTION:
		return "SignonErrorDefunctConnection";
	case E_MESSENGER_SIGNON_ERROR_UNKNOWN_FAILURE:
		return "SignonErrorUnknownFailure";
	default:
		return "Unidentified Signon Error";
	}
}
