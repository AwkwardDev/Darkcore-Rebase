/*
 * Copyright (C) 2011-2012 DarkCore <http://www.darkpeninsula.eu/>
 * Copyright (C) 2011-2012 Project SkyFire <http://www.projectskyfire.org/>
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2012 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#include "Common.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "GuildMgr.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "DatabaseEnv.h"
#include "CellImpl.h"
#include "Chat.h"
#include "ChannelMgr.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Guild.h"
#include "Language.h"
#include "Log.h"
#include "Opcodes.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellAuraEffects.h"
#include "Util.h"
#include "ScriptMgr.h"
#include "AccountMgr.h"

bool WorldSession::processChatmessageFurtherAfterSecurityChecks(std::string& msg, uint32 lang)
{
    if (lang != LANG_ADDON)
    {
        // strip invisible characters for non-addon messages
        if (sWorld->getBoolConfig(CONFIG_CHAT_FAKE_MESSAGE_PREVENTING))
            stripLineInvisibleChars(msg);

        if (sWorld->getIntConfig(CONFIG_CHAT_STRICT_LINK_CHECKING_SEVERITY) && AccountMgr::IsPlayerAccount(GetSecurity())
                && !ChatHandler(this).isValidChatMessage(msg.c_str()))
        {
            sLog->outError("Player %s (GUID: %u) sent a chat message with an invalid link: %s", GetPlayer()->GetName(),
                    GetPlayer()->GetGUIDLow(), msg.c_str());
            if (sWorld->getIntConfig(CONFIG_CHAT_STRICT_LINK_CHECKING_KICK))
                KickPlayer();
            return false;
        }
    }
    return true;
}

void WorldSession::HandleMessagechatOpcode(WorldPacket & recv_data)
{
    uint32 type;
    uint32 lang;

    switch (recv_data.GetOpcode())
    {
        case CMSG_MESSAGECHAT_SAY:
            type = CHAT_MSG_SAY;
            break;
        case CMSG_MESSAGECHAT_YELL:
            type = CHAT_MSG_YELL;
            break;
        case CMSG_MESSAGECHAT_CHANNEL:
            type = CHAT_MSG_CHANNEL;
            break;
        case CMSG_MESSAGECHAT_WHISPER:
            type = CHAT_MSG_WHISPER;
            break;
        case CMSG_MESSAGECHAT_GUILD:
            type = CHAT_MSG_GUILD;
            break;
        case CMSG_MESSAGECHAT_OFFICER:
            type = CHAT_MSG_OFFICER;
            break;
        case CMSG_MESSAGECHAT_AFK:
            type = CHAT_MSG_AFK;
            break;
        case CMSG_MESSAGECHAT_DND:
            type = CHAT_MSG_DND;
            break;
        case CMSG_MESSAGECHAT_EMOTE:
            type = CHAT_MSG_EMOTE;
            break;
        case CMSG_MESSAGECHAT_PARTY:
            type = CHAT_MSG_PARTY;
            break;
        case CMSG_MESSAGECHAT_PARTY_LEADER:
            type = CHAT_MSG_PARTY_LEADER;
            break;
        case CMSG_MESSAGECHAT_RAID:
            type = CHAT_MSG_RAID;
            break;
        case CMSG_MESSAGECHAT_RAID_LEADER:
            type = CHAT_MSG_RAID_LEADER;
            break;
        case CMSG_MESSAGECHAT_BATTLEGROUND:
            type = CHAT_MSG_BATTLEGROUND;
            break;
        case CMSG_MESSAGECHAT_BATTLEGROUND_LEADER:
            type = CHAT_MSG_BATTLEGROUND_LEADER;
            break;
        case CMSG_MESSAGECHAT_RAID_WARNING:
            type = CHAT_MSG_RAID_WARNING;
            break;
        default:
            break;
    }

    if (type != CHAT_MSG_EMOTE && type != CHAT_MSG_AFK && type != CHAT_MSG_DND)
        recv_data >> lang;
    else
        lang = LANG_UNIVERSAL;

    sLog->outDebug(LOG_FILTER_NETWORKIO, "CHAT: packet received. type %u, lang %u", type, lang );

    std::string msg = "";
    recv_data >> msg;

    std::string channelOrWhisperName;
    if (type == CHAT_MSG_CHANNEL || type == CHAT_MSG_WHISPER)
        recv_data >> channelOrWhisperName;

    if (msg.empty() && (type != CHAT_MSG_AFK && type != CHAT_MSG_DND))
        return;

    if (type != CHAT_MSG_AFK && type != CHAT_MSG_DND)
        if (ChatHandler(this).ParseCommands(msg.c_str()))
            return;

    if (!processChatmessageFurtherAfterSecurityChecks(msg, lang))
        return;

    Player* sender = GetPlayer();

    if (type != CHAT_MSG_EMOTE)
    {
        // prevent talking at unknown language (cheating)
        LanguageDesc const* langDesc = GetLanguageDescByID(lang);
        if (!langDesc)
        {
            SendNotification(LANG_UNKNOWN_LANGUAGE);
            return;
        }
        if (langDesc->skill_id != 0 && !sender->HasSkill(langDesc->skill_id))
        {
            // also check SPELL_AURA_COMPREHEND_LANGUAGE (client offers option to speak in that language)
            Unit::AuraEffectList const& langAuras = sender->GetAuraEffectsByType(SPELL_AURA_COMPREHEND_LANGUAGE);
            bool foundAura = false;
            for (Unit::AuraEffectList::const_iterator i = langAuras.begin(); i != langAuras.end(); ++i)
            {
                if ((*i)->GetMiscValue() == int32(lang))
                {
                    foundAura = true;
                    break;
                }
            }
            if (!foundAura)
            {
                SendNotification(LANG_NOT_LEARNED_LANGUAGE);
                return;
            }
        }
    }

    if (lang == LANG_ADDON)
    {
        sScriptMgr->OnPlayerChat(sender, uint32(CHAT_MSG_ADDON), lang, msg);

        // Disabled addon channel?
        if (!sWorld->getBoolConfig(CONFIG_ADDON_CHANNEL))
            return;
    }
    // LANG_ADDON should not be changed nor be affected by flood control
    else
    {
        // send in universal language if player in .gmon mode (ignore spell effects)
        if (sender->isGameMaster())
            lang = LANG_UNIVERSAL;
        else
        {
            // send in universal language in two side iteration allowed mode
            if (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_CHAT))
                lang = LANG_UNIVERSAL;
            else
            {
                switch (type)
                {
                    case CHAT_MSG_PARTY:
                    case CHAT_MSG_PARTY_LEADER:
                    case CHAT_MSG_RAID:
                    case CHAT_MSG_RAID_LEADER:
                    case CHAT_MSG_RAID_WARNING:
                        // allow two side chat at group channel if two side group allowed
                        if (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GROUP))
                            lang = LANG_UNIVERSAL;
                        break;
                    case CHAT_MSG_GUILD:
                    case CHAT_MSG_OFFICER:
                        // allow two side chat at guild channel if two side guild allowed
                        if (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD))
                            lang = LANG_UNIVERSAL;
                        break;
                }
            }

            // but overwrite it by SPELL_AURA_MOD_LANGUAGE auras (only single case used)
            Unit::AuraEffectList const& ModLangAuras = sender->GetAuraEffectsByType(SPELL_AURA_MOD_LANGUAGE);
            if (!ModLangAuras.empty())
                lang = ModLangAuras.front()->GetMiscValue();
        }

        if (!sender->CanSpeak())
        {
            std::string timeStr = secsToTimeString(m_muteTime - time(NULL));
            SendNotification(GetCoreString(LANG_WAIT_BEFORE_SPEAKING), timeStr.c_str());
            recv_data.rfinish(); // Prevent warnings
            return;
        }

        if (type != CHAT_MSG_AFK && type != CHAT_MSG_DND)
            sender->UpdateSpeakTime();
    }

    if (sender->HasAura(1852) && type != CHAT_MSG_WHISPER)
    {
        std::string msg = "";
        recv_data >> msg;

        SendNotification(GetCoreString(LANG_GM_SILENCE), sender->GetName());
        return;
    }

    switch (type)
    {
        case CHAT_MSG_SAY:
        case CHAT_MSG_EMOTE:
        case CHAT_MSG_YELL:
        {
            if (sender->getLevel() < sWorld->getIntConfig(CONFIG_CHAT_SAY_LEVEL_REQ))
            {
                SendNotification(GetCoreString(LANG_SAY_REQ), sWorld->getIntConfig(CONFIG_CHAT_SAY_LEVEL_REQ));
                return;
            }

            if (type == CHAT_MSG_SAY)
                sender->Say(msg, lang);
            else if (type == CHAT_MSG_EMOTE)
                sender->TextEmote(msg);
            else if (type == CHAT_MSG_YELL)
                sender->Yell(msg, lang);
        } break;
        case CHAT_MSG_WHISPER:
        {
            if (sender->getLevel() < sWorld->getIntConfig(CONFIG_CHAT_WHISPER_LEVEL_REQ))
            {
                SendNotification(GetCoreString(LANG_WHISPER_REQ), sWorld->getIntConfig(CONFIG_CHAT_WHISPER_LEVEL_REQ));
                return;
            }

            if (!normalizePlayerName(channelOrWhisperName))
            {
                SendPlayerNotFoundNotice(channelOrWhisperName);
                break;
            }

            Player* receiver = sObjectAccessor->FindPlayerByName(channelOrWhisperName.c_str());
            bool senderIsPlayer = AccountMgr::IsPlayerAccount(GetSecurity());
            bool receiverIsPlayer = AccountMgr::IsPlayerAccount(receiver ? receiver->GetSession()->GetSecurity() : SEC_PLAYER);
            if (!receiver || (senderIsPlayer && !receiverIsPlayer && !receiver->isAcceptWhispers() && !receiver->IsInWhisperWhiteList(sender->GetGUID())))
            {
                SendPlayerNotFoundNotice(channelOrWhisperName);
                return;
            }

            if (!sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_CHAT) && senderIsPlayer && receiverIsPlayer)
                if (GetPlayer()->GetTeam() != receiver->GetTeam())
                {
                    SendWrongFactionNotice();
                    return;
                }

            if (GetPlayer()->HasAura(1852) && !receiver->isGameMaster())
            {
                SendNotification(GetCoreString(LANG_GM_SILENCE), GetPlayer()->GetName());
                return;
            }

            // If player is a Gamemaster and doesn't accept whisper, we auto-whitelist every player that the Gamemaster is talking to
            if (!senderIsPlayer && !sender->isAcceptWhispers() && !sender->IsInWhisperWhiteList(receiver->GetGUID()))
                sender->AddWhisperWhiteList(receiver->GetGUID());

            GetPlayer()->Whisper(msg, lang, receiver->GetGUID());
        } break;
        case CHAT_MSG_PARTY:
        case CHAT_MSG_PARTY_LEADER:
        {
            // if player is in battleground, he cannot say to battleground members by /p
            Group* group = GetPlayer()->GetOriginalGroup();
            if (!group)
            {
                group = sender->GetGroup();
                if (!group || group->isBGGroup())
                    return;
            }

            if (type == CHAT_MSG_PARTY_LEADER && !group->IsLeader(sender->GetGUID()))
                return;

            sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, group);

            WorldPacket data;
            ChatHandler::FillMessageData(&data, this, uint8(type), lang, NULL, 0, msg.c_str(), NULL);
            group->BroadcastPacket(&data, false, group->GetMemberGroup(GetPlayer()->GetGUID()));
        } break;
        case CHAT_MSG_GUILD:
        {
            if (GetPlayer()->GetGuildId())
            {
                if (Guild* guild = sGuildMgr->GetGuildById(GetPlayer()->GetGuildId()))
                {
                    sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, guild);

                    guild->BroadcastToGuild(this, false, msg, lang == LANG_ADDON ? LANG_ADDON : LANG_UNIVERSAL);
                }
            }
        } break;
        case CHAT_MSG_OFFICER:
        {
            if (GetPlayer()->GetGuildId())
            {
                if (Guild* guild = sGuildMgr->GetGuildById(GetPlayer()->GetGuildId()))
                {
                    sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, guild);

                    guild->BroadcastToGuild(this, true, msg, lang == LANG_ADDON ? LANG_ADDON : LANG_UNIVERSAL);
                }
            }
        } break;
        case CHAT_MSG_RAID:
        {
            // if player is in battleground, he cannot say to battleground members by /ra
            Group* group = GetPlayer()->GetOriginalGroup();
            if (!group)
            {
                group = GetPlayer()->GetGroup();
                if (!group || group->isBGGroup() || !group->isRaidGroup())
                    return;
            }

            sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, group);

            WorldPacket data;
            ChatHandler::FillMessageData(&data, this, CHAT_MSG_RAID, lang, "", 0, msg.c_str(), NULL);
            group->BroadcastPacket(&data, false);
        } break;
        case CHAT_MSG_RAID_LEADER:
        {
            // if player is in battleground, he cannot say to battleground members by /ra
            Group* group = GetPlayer()->GetOriginalGroup();
            if (!group)
            {
                group = GetPlayer()->GetGroup();
                if (!group || group->isBGGroup() || !group->isRaidGroup() || !group->IsLeader(sender->GetGUID()))
                    return;
            }

            sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, group);

            WorldPacket data;
            ChatHandler::FillMessageData(&data, this, CHAT_MSG_RAID_LEADER, lang, "", 0, msg.c_str(), NULL);
            group->BroadcastPacket(&data, false);
        } break;
        case CHAT_MSG_RAID_WARNING:
        {
            Group* group = GetPlayer()->GetGroup();
            if (!group || !group->isRaidGroup() || !(group->IsLeader(GetPlayer()->GetGUID()) || group->IsAssistant(GetPlayer()->GetGUID())) || group->isBGGroup())
                return;

            sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, group);

            WorldPacket data;
            //in battleground, raid warning is sent only to players in battleground - code is ok
            ChatHandler::FillMessageData(&data, this, CHAT_MSG_RAID_WARNING, lang, "", 0, msg.c_str(), NULL);
            group->BroadcastPacket(&data, false);
        } break;
        case CHAT_MSG_BATTLEGROUND:
        {
            //battleground raid is always in Player->GetGroup(), never in GetOriginalGroup()
            Group* group = GetPlayer()->GetGroup();
            if (!group || !group->isBGGroup())
                return;

            sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, group);

            WorldPacket data;
            ChatHandler::FillMessageData(&data, this, CHAT_MSG_BATTLEGROUND, lang, "", 0, msg.c_str(), NULL);
            group->BroadcastPacket(&data, false);
        } break;
        case CHAT_MSG_BATTLEGROUND_LEADER:
        {
            // battleground raid is always in Player->GetGroup(), never in GetOriginalGroup()
            Group* group = GetPlayer()->GetGroup();
            if (!group || !group->isBGGroup() || !group->IsLeader(GetPlayer()->GetGUID()))
                return;

            sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, group);

            WorldPacket data;
            ChatHandler::FillMessageData(&data, this, CHAT_MSG_BATTLEGROUND_LEADER, lang, "", 0, msg.c_str(), NULL);
            group->BroadcastPacket(&data, false);
        } break;
        case CHAT_MSG_CHANNEL:
        {
            if (AccountMgr::IsPlayerAccount(GetSecurity()))
            {
                if (sender->getLevel() < sWorld->getIntConfig(CONFIG_CHAT_CHANNEL_LEVEL_REQ))
                {
                    SendNotification(GetCoreString(LANG_CHANNEL_REQ), sWorld->getIntConfig(CONFIG_CHAT_CHANNEL_LEVEL_REQ));
                    return;
                }
            }

            if (ChannelMgr* cMgr = channelMgr(sender->GetTeam()))
            {
                if (Channel* chn = cMgr->GetChannel(channelOrWhisperName, sender))
                {
                    sScriptMgr->OnPlayerChat(sender, type, lang, msg, chn);

                    chn->Say(sender->GetGUID(), msg.c_str(), lang);
                }
            }
        } break;
        case CHAT_MSG_AFK:
        {
            if (!sender->isInCombat())
            {
                if (!sender->isAFK())
                {
                    if (msg.empty())
                        msg  = GetCoreString(LANG_PLAYER_AFK_DEFAULT);
                    sender->afkMsg = msg;
                }

                sScriptMgr->OnPlayerChat(_player, type, lang, msg);

                _player->ToggleAFK();
                if (_player->isAFK() && _player->isDND())
                    _player->ToggleDND();
            }
        } break;
        case CHAT_MSG_DND:
        {
            if (!_player->isDND())
            {
                if (msg.empty())
                    msg = GetCoreString(LANG_PLAYER_DND_DEFAULT);
                _player->dndMsg = msg;
            }

            sScriptMgr->OnPlayerChat(_player, type, lang, msg);

            _player->ToggleDND();
            if (_player->isDND() && _player->isAFK())
                _player->ToggleAFK();
        } break;
        default:
            sLog->outError("CHAT: unknown message type %u, lang: %u", type, lang);
            break;
    }
}

void WorldSession::HandleEmoteOpcode(WorldPacket & recv_data)
{
    if (!GetPlayer()->isAlive() || GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        return;

    uint32 emote;
    recv_data >> emote;
    sScriptMgr->OnPlayerEmote(GetPlayer(), emote);
    GetPlayer()->HandleEmoteCommand(emote);
}

namespace Darkcore
{
    class EmoteChatBuilder
    {
        public:
            EmoteChatBuilder(Player const& player, uint32 text_emote, uint32 emote_num, Unit const* target)
                : i_player(player), i_text_emote(text_emote), i_emote_num(emote_num), i_target(target) {}

            void operator()(WorldPacket& data, LocaleConstant loc_idx)
            {
                char const* nam = i_target ? i_target->GetNameForLocaleIdx(loc_idx) : NULL;
                uint32 namlen = (nam ? strlen(nam) : 0) + 1;

                data.Initialize(SMSG_TEXT_EMOTE, (20+namlen));
                data << i_player.GetGUID();
                data << (uint32)i_text_emote;
                data << i_emote_num;
                data << (uint32)namlen;
                if (namlen > 1)
                    data.append(nam, namlen);
                else
                    data << (uint8)0x00;
            }

        private:
            Player const& i_player;
            uint32        i_text_emote;
            uint32        i_emote_num;
            Unit const*   i_target;
    };
}                                                           // namespace Darkcore

void WorldSession::HandleTextEmoteOpcode(WorldPacket & recv_data)
{
    if (!GetPlayer()->isAlive())
        return;

    if (!GetPlayer()->CanSpeak())
    {
        std::string timeStr = secsToTimeString(m_muteTime - time(NULL));
        SendNotification(GetCoreString(LANG_WAIT_BEFORE_SPEAKING), timeStr.c_str());
        return;
    }

    uint32 text_emote, emoteNum;
    uint64 guid;

    recv_data >> text_emote;
    recv_data >> emoteNum;
    recv_data >> guid;

    sScriptMgr->OnPlayerTextEmote(GetPlayer(), text_emote, emoteNum, guid);

    EmotesTextEntry const* em = sEmotesTextStore.LookupEntry(text_emote);
    if (!em)
        return;

    uint32 emote_anim = em->textid;

    switch (emote_anim)
    {
        case EMOTE_STATE_SLEEP:
        case EMOTE_STATE_SIT:
        case EMOTE_STATE_KNEEL:
        case EMOTE_ONESHOT_NONE:
            break;
        default:
            // Only allow text-emotes for "dead" entities (feign death included)
            if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
                break;
             GetPlayer()->HandleEmoteCommand(emote_anim);
             break;
    }

    Unit* unit = ObjectAccessor::GetUnit(*_player, guid);

    CellCoord p = Darkcore::ComputeCellCoord(GetPlayer()->GetPositionX(), GetPlayer()->GetPositionY());

    Cell cell(p);
    cell.SetNoCreate();

    Darkcore::EmoteChatBuilder emote_builder(*GetPlayer(), text_emote, emoteNum, unit);
    Darkcore::LocalizedPacketDo<Darkcore::EmoteChatBuilder > emote_do(emote_builder);
    Darkcore::PlayerDistWorker<Darkcore::LocalizedPacketDo<Darkcore::EmoteChatBuilder > > emote_worker(GetPlayer(), sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_TEXTEMOTE), emote_do);
    TypeContainerVisitor<Darkcore::PlayerDistWorker<Darkcore::LocalizedPacketDo<Darkcore::EmoteChatBuilder> >, WorldTypeMapContainer> message(emote_worker);
    cell.Visit(p, message, *GetPlayer()->GetMap(), *GetPlayer(), sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_TEXTEMOTE));

    GetPlayer()->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_DO_EMOTE, text_emote, 0, unit);

    //Send scripted event call
    if (unit && unit->GetTypeId() == TYPEID_UNIT && ((Creature*)unit)->AI())
        ((Creature*)unit)->AI()->ReceiveEmote(GetPlayer(), text_emote);
}

void WorldSession::HandleChatIgnoredOpcode(WorldPacket& recv_data)
{
    uint64 iguid;
    uint8 unk;
    //sLog->outDebug(LOG_FILTER_PACKETIO, "WORLD: Received CMSG_CHAT_IGNORED");

    recv_data >> iguid;
    recv_data >> unk;                                       // probably related to spam reporting

    Player* player = ObjectAccessor::FindPlayer(iguid);
    if (!player || !player->GetSession())
        return;

    WorldPacket data;
    ChatHandler::FillMessageData(&data, this, CHAT_MSG_IGNORED, LANG_UNIVERSAL, NULL, GetPlayer()->GetGUID(), GetPlayer()->GetName(), NULL);
    player->GetSession()->SendPacket(&data);
}

void WorldSession::HandleChannelDeclineInvite(WorldPacket &recvPacket)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "Opcode %u", recvPacket.GetOpcode());
}

void WorldSession::SendPlayerNotFoundNotice(std::string name)
{
    WorldPacket data(SMSG_CHAT_PLAYER_NOT_FOUND, name.size()+1);
    data << name;
    SendPacket(&data);
}

void WorldSession::SendPlayerAmbiguousNotice(std::string name)
{
    WorldPacket data(SMSG_CHAT_PLAYER_AMBIGUOUS, name.size()+1);
    data << name;
    SendPacket(&data);
}

void WorldSession::SendWrongFactionNotice()
{
    WorldPacket data(SMSG_CHAT_WRONG_FACTION, 0);
    SendPacket(&data);
}

void WorldSession::SendChatRestrictedNotice(ChatRestrictionType restriction)
{
    WorldPacket data(SMSG_CHAT_RESTRICTED, 1);
    data << uint8(restriction);
    SendPacket(&data);
}
