#pragma once

namespace im::mq {

inline constexpr const char* kMessageCreated = "im.message.created";
inline constexpr const char* kMessageDelivered = "im.message.delivered";
inline constexpr const char* kFriendEvents = "im.friend.events";
inline constexpr const char* kPresence = "im.presence";
inline constexpr const char* kMessageDlq = "im.message.dlq";
inline constexpr const char* kAudit = "im.audit";

}  // namespace im::mq
