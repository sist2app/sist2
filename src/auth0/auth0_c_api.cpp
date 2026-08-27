#include "auth0_c_api.h"
#include "jwt/jwt.hpp"
#include "iostream"
#include "cjson/cJSON.h"

int auth0_verify_jwt(const char *secret_str, const char *token, const char *audience) {
    using namespace jwt::params;

    jwt::jwt_object object;
    try {
        object = jwt::decode(
                token,
                algorithms({"RS256"}),
                secret(secret_str),
                verify(true)
        );
    } catch (const jwt::TokenExpiredError& e) {
        return AUTH0_ERR_EXPIRED;
    } catch (const jwt::SignatureFormatError& e) {
        return AUTH0_ERR_SIG_FORMAT;
    } catch (const jwt::DecodeError& e) {
        return AUTH0_ERR_DECODE;
    } catch (const jwt::VerificationError& e) {
        return AUTH0_ERR_VERIFICATION;
    }

    std::stringstream buf;
    buf << object.payload();
    std::string json_payload_str = buf.str();
    cJSON *payload = cJSON_Parse(json_payload_str.c_str());

    bool audience_ok = false;
    cJSON *aud;
    cJSON_ArrayForEach(aud, cJSON_GetObjectItem(payload, "aud")) {
        if (aud != nullptr && strcmp(aud->valuestring, audience) == 0) {
            audience_ok = true;
        }
    }
    cJSON_Delete(payload);

    if (!audience_ok) {
        return AUTH0_ERR_AUDIENCE;
    }

    return AUTH0_OK;
}
