

class Exports:
	pass

# list of [{'name': X, 'root': X, 'from': X, 'fromfrom': X, 'fromfromfrom': X}]
Exports.OnActions = []

seen_names = set()

def add_action(name, root_scope, from_scope=None, from_from_scope=None, from_from_from_scope=None, this_scope=None):
	if name in seen_names:
		raise Exception("Duplicate on_action " + name)
		
	seen_names.add(name)

	class Obj: pass
	Obj.name = name
	Obj.root_scope = root_scope
	Obj.this_scope = this_scope
	Obj.from_scope = from_scope
	Obj.from_from_scope = from_from_scope
	Obj.from_from_from_scope = from_from_from_scope
	effect_scope_target_name = root_scope + 'Command'
	if this_scope:
		effect_scope_target_name += 'This' + this_scope
	if from_scope:
		effect_scope_target_name += 'From' + from_scope
	if from_from_scope:
		effect_scope_target_name += 'FromFrom' + from_from_scope
	if from_from_from_scope:
		effect_scope_target_name += 'FromFromFrom' + from_from_from_scope
	Obj.effect_scope_target_name = effect_scope_target_name
	Exports.OnActions.append(Obj)

# root=char, from=char
for name in [
	'on_law_vote_passed',
	'on_law_vote_failed',
	'on_society_bi_yearly_pulse',
	'on_marriage',
	'on_betrothal',
	'on_divorce',
	'on_feud_started',
	'on_feud_ended',
	'on_battle_won_owner',
	'on_battle_lost_owner',
	'on_become_imprisoned_any_reason',
	'on_tyranny_gained',
	'on_absorb_clan_attempted_started_war',
	'on_split_clan_attempted_started_war',
	'on_war_ended_defeat',
	'on_war_ended_whitepeace',
	'on_war_ended_victory',
	'on_war_ended_invalid',
	'on_war_started',
	'on_failed_assassination',
	'on_failed_assassination_disc',
	'on_assassination',
	'on_assassination_disc',
	'on_bastard_birth',
	'on_pregnancy',
	'on_bastard_pregnancy',
	'on_avoided_imprison_started_war',
	'on_became_imprisoned',
	'on_avoided_imprison_fled_country',
	'on_released_from_prison',
	'on_executed',
	'on_exiled',
	'on_prepared_invasion_monthly',
	'on_prepared_invasion_expires',
	'on_prepared_invasion_aborts',
	'on_forced_consort',
	'on_chronicle_owner_change',
	'on_elective_gavelkind_succession',
	'on_rel_elector_chosen',
	'on_rel_head_chosen',
	'on_crusade_monthly',
]:
	add_action(name, root_scope='Char', from_scope='Char')

# root=title, from=char, this=char
# HOWEVER we don't do this because ROOT/THIS handling doesn't work correctly
for name in [
	'on_crusade_failure',
	'on_crusade_success',
	'on_crusade_invalid',
]:
	add_action(name, root_scope='Char', from_scope='Char')

# root=char, from=title
for name in [
	'councilor_on_approve_law',
	'on_siege_over',
	'on_approve_de_jure_law',
	'on_approve_law',
	'on_siege_over_winner',
	'on_siege_over_loc_chars',
	'on_siege_won_leader',
	'on_siege_lost_leader',
	'on_settlement_construction_start',
	'on_settlement_construction_completed',
	'on_holding_building_start',
	'on_crusade_creation',
	'on_create_title',
	'on_settlement_looted',
	'on_loot_settlement',
]:
	add_action(name, root_scope='Char', from_scope='Title')

# root=char, from=society
for name in [
	'on_character_switch_society_interest',
	'on_character_society_rank_up',
	'on_character_society_rank_down',
	'on_character_join_society',
	'on_character_leave_society',
	'on_character_ask_to_join_society',
	'on_character_kicked_from_society',
	'on_character_stop_showing_interest',
	'on_society_created',
	'on_society_destroyed',
	'on_society_progress_zero',
	'on_society_progress_full',
]:
	add_action(name, root_scope='Char', from_scope='Society')

# root=title, from=char, fromfrom=char, this=char
# HOWEVER we don't do this because ROOT/THIS handling doesn't work correctly
for name in [
	'GRANT_LANDED_TITLE_INTERACTION_ACCEPT_EVENT',
	'REVOKE_TITLE_INTERACTION_ACCEPT_EVENT',
	'REVOKE_TITLE_INTERACTION_DECLINE_EVENT',
	'SPLIT_CLAN_INTERACTION_ACCEPT_EVENT',
	'SPLIT_CLAN_INTERACTION_DECLINE_EVENT',
    'GRANT_VICE_ROYALTY_INTERACTION_ACCEPT_EVENT',
]:
	add_action(name, root_scope='Char', from_scope='Char', from_from_scope='Char')

# root=char, from=char, fromfrom=char
for name in [
	# http://forum.paradoxplaza.com/forum/showthread.php?597480-The-Validator-Find-errors-quickly-and-with-minimal-pain!&p=18247155&viewfull=1#post18247155
	'DEMAND_RELIGIOUS_CONVERSION_INTERACTION_ACCEPT_EVENT',
	'DEMAND_RELIGIOUS_CONVERSION_INTERACTION_DECLINE_EVENT',
	'OFFER_PEACE_INTERACTION_ACCEPT_EVENT',
	'OFFER_PEACE_INTERACTION_DECLINE_EVENT',
	'SEND_GIFT_INTERACTION_ACCEPT_EVENT',
	'SEND_GIFT_INTERACTION_DECLINE_EVENT',
	'ASK_FOR_MONEY_INTERACTION_ACCEPT_EVENT',
	'ASK_FOR_MONEY_INTERACTION_DECLINE_EVENT',
	'ASK_FOR_CLAIM_INTERACTION_ACCEPT_EVENT',
	'ASK_FOR_CLAIM_INTERACTION_DECLINE_EVENT',
	'NOMINATE_BISHOP_TO_POPE_INTERACTION_ACCEPT_EVENT',
	'NOMINATE_BISHOP_TO_POPE_INTERACTION_DECLINE_EVENT',
	'OFFER_VASSALIZATION_INTERACTION_ACCEPT_EVENT',
	'OFFER_VASSALIZATION_INTERACTION_DECLINE_EVENT',
	'ARRANGE_BETROTHAL_INTERACTION_ACCEPT_EVENT',
	'ARRANGE_BETROTHAL_INTERACTION_DECLINE_EVENT',
	'ARRANGE_SUCC_BETROTHAL_INTERACTION_ACCEPT_EVENT',
	'ARRANGE_SUCC_BETROTHAL_INTERACTION_DECLINE_EVENT',
	'OFFER_SUCCESSION_MARRIGE_INTERACTION_ACCEPT_EVENT',
	'OFFER_SUCCESSION_MARRIGE_INTERACTION_DECLINE_EVENT',
	'OFFER_MARRIGE_INTERACTION_ACCEPT_EVENT',
	'OFFER_MARRIGE_INTERACTION_DECLINE_EVENT',
	'RANSOM_CHARACTER_INTERACTION_ACCEPT_EVENT',
	'RANSOM_CHARACTER_INTERACTION_DECLINE_EVENT',
	'EDUCATE_CHARACTER_INTERACTION_ACCEPT_EVENT',
	'EDUCATE_CHARACTER_INTERACTION_DECLINE_EVENT',
	'ABANDON_AMBITION_INTERACTION_ACCEPT_EVENT',
	'ABANDON_AMBITION_INTERACTION_DECLINE_EVENT',
	'STOP_BACKING_AMBITION_INTERACTION_ACCEPT_EVENT',
	'STOP_BACKING_AMBITION_INTERACTION_DECLINE_EVENT',
	'JOIN_AMBITION_INTERACTION_ACCEPT_EVENT',
	'JOIN_AMBITION_INTERACTION_DECLINE_EVENT',
	'GRANT_LANDED_TITLE_INTERACTION_DECLINE_EVENT',
	'APPOINT_TO_OFFICE_INTERACTION_ACCEPT_EVENT',
	'APPOINT_TO_OFFICE_INTERACTION_DECLINE_EVENT',
	'ASK_FOR_INVASION_INTERACTION_ACCEPT_EVENT',
	'ASK_FOR_INVASION_INTERACTION_DECLINE_EVENT',
	'ASK_FOR_DIVORCE_INTERACTION_ACCEPT_EVENT',
	'ASK_FOR_DIVORCE_INTERACTION_DECLINE_EVENT',
	'ASK_FOR_EXCOMMUNICATION_INTERACTION_ACCEPT_EVENT',
	'ASK_FOR_EXCOMMUNICATION_INTERACTION_DECLINE_EVENT',
	'CALL_ALLY_INTERACTION_ACCEPT_EVENT',
	'CALL_ALLY_INTERACTION_DECLINE_EVENT',
	'DECLARE_WAR_INTERACTION_ACCEPT_EVENT',
	'RETRACT_VASSAL_INTERACTION_ACCEPT_EVENT',
	'RETRACT_VASSAL_INTERACTION_DECLINE_EVENT',
	'INVITE_TO_COURT_INTERACTION_ACCEPT_EVENT',
	'INVITE_TO_COURT_INTERACTION_DECLINE_EVENT',
	'ASK_FOR_VASSALIZATION_INTERACTION_ACCEPT_EVENT',
	'ASK_FOR_VASSALIZATION_INTERACTION_DECLINE_EVENT',
	'ASK_TO_LIFT_EXCOMMUNICATION_INTERACTION_ACCEPT_EVENT',
	'ASK_TO_LIFT_EXCOMMUNICATION_INTERACTION_DECLINE_EVENT',
	'ASK_TO_RANSOM_CHARACTER_INTERACTION_ACCEPT_EVENT',
	'ASK_TO_RANSOM_CHARACTER_INTERACTION_DECLINE_EVENT',
	'SETTLE_ADVENTURER_INTERACTION_ACCEPT_EVENT',
	'SETTLE_ADVENTURER_INTERACTION_DECLINE_EVENT',
	'ASK_TO_JOIN_WAR_INTERACTION_ACCEPT_EVENT',
	'ASK_TO_JOIN_WAR_INTERACTION_DECLINE_EVENT',
	'ASK_TO_EMBARGO_INTERACTION_ACCEPT_EVENT',
	'ASK_TO_EMBARGO_INTERACTION_DECLINE_EVENT',
	'MAKE_CONSORT_INTERACTION_ACCEPT_EVENT',
	'MAKE_CONSORT_INTERACTION_DECLINE_EVENT',
	'ABSORB_CLAN_INTERACTION_ACCEPT_EVENT',
	'ABSORB_CLAN_INTERACTION_DECLINE_EVENT',
	'FORM_BLOOD_OATH_INTERACTION_ACCEPT_EVENT',
	'FORM_BLOOD_OATH_INTERACTION_DECLINE_EVENT',
	'SETTLE_FEUD_INTERACTION_ACCEPT_EVENT',
	'SETTLE_FEUD_INTERACTION_DECLINE_EVENT',
	'FORM_ALLIANCE_INTERACTION_ACCEPT_EVENT',
	'FORM_ALLIANCE_INTERACTION_DECLINE_EVENT',
	'DISSOLVE_ALLIANCE_INTERACTION_ACCEPT_EVENT',
	'DISSOLVE_ALLIANCE_INTERACTION_DECLINE_EVENT',
	'BUY_FAVOR_INTERACTION_ACCEPT_EVENT',
	'BUY_FAVOR_INTERACTION_DECLINE_EVENT',
	'REQUEST_SUPPORT_INTERACTION_ACCEPT_EVENT',
	'REQUEST_SUPPORT_INTERACTION_DECLINE_EVENT',
	'FORM_NON_AGGRESSION_PACT_INTERACTION_ACCEPT_EVENT',
	'FORM_NON_AGGRESSION_PACT_INTERACTION_DECLINE_EVENT',
	'ASK_COUNCIL_POSITION_INTERACTION_ACCEPT_EVENT',
	'ASK_COUNCIL_POSITION_INTERACTION_DECLINE_EVENT',
	'ASK_REALM_PEACE_INTERACTION_ACCEPT_EVENT',
	'ASK_REALM_PEACE_INTERACTION_DECLINE_EVENT',
	'FORCE_JOIN_FACTION_INTERACTION_ACCEPT_EVENT',
	'FORCE_JOIN_FACTION_INTERACTION_DECLINE_EVENT',
	'ASK_TO_JOIN_AMBITION_INTERACTION_ACCEPT_EVENT',
	'ASK_TO_JOIN_AMBITION_INTERACTION_DECLINE_EVENT',
	'OFFER_CONSORT_INTERACTION_ACCEPT_EVENT',
	'GIVE_ARTIFACT_INTERACTION_ACCEPT_EVENT',
    
    'ASK_FOR_CRUSADE_INTERACTION_ACCEPT_EVENT',
    'ASK_FOR_CRUSADE_INTERACTION_DECLINE_EVENT',
    'ASK_TO_DECLARE_WAR_INTERACTION_ACCEPT_EVENT',
    'ASK_TO_DECLARE_WAR_INTERACTION_DECLINE_EVENT',
    'APPOINT_COMMANDER_INTERACTION_ACCEPT_EVENT',
    'ASSIGN_VOTER_TITLE_INTERACTION_ACCEPT_EVENT',
    'AWARD_HONORARY_TITLE_INTERACTION_ACCEPT_EVENT',
    'BREAK_BETROTHAL_INTERACTION_ACCEPT_EVENT',
    'CALL_IN_FAVOR_INTERACTION_ACCEPT_EVENT',
    'CALL_IN_FAVOR_SUCCESSION_VOTE_ACCEPT_EVENT',
    'CHANGE_CRUSADE_TARGET_INTERACTION_ACCEPT_EVENT',
    'DISMISS_CONSORT_INTERACTION_ACCEPT_EVENT',
    'DIVORCE_INTERACTION_ACCEPT_EVENT',
    'EXECUTE_IMPRISONED_INTERACTION_ACCEPT_EVENT',
    'EXILE_IMPRISONED_INTERACTION_ACCEPT_EVENT',
    'IMPRISON_INTERACTION_ACCEPT_EVENT',
    'LEAVE_COALITION_ACCEPT_EVENT',
    'PREPARE_INVASION_INTERACTION_ACCEPT_EVENT',
    'RELEASE_FROM_PRISON_INTERACTION_ACCEPT_EVENT',
    'RELEASE_VASSAL_INTERACTION_ACCEPT_EVENT',
    'REPLACE_MERCENARY_CAPTAIN_INTERACTION_ACCEPT_EVENT',
    'RESIGN_AS_COMMANDER_INTERACTION_ACCEPT_EVENT',
    'RESIGN_COMMANDER_INTERACTION_ACCEPT_EVENT',
    'RESIGN_FROM_OFFICE_INTERACTION_ACCEPT_EVENT',
    'REVOKE_HONORARY_TITLE_INTERACTION_ACCEPT_EVENT',
    'REVOKE_VOTER_TITLE_INTERACTION_ACCEPT_EVENT',
    'SEND_ASSASSIN_INTERACTION_ACCEPT_EVENT',
    'START_COALITION_ACCEPT_EVENT',
    'STOP_ENFORCE_PEACE_INTERACTION_ACCEPT_EVENT',
    'TRANSFER_VASSAL_INTERACTION_ACCEPT_EVENT',
    # Doesn't work - https://forum.paradoxplaza.com/forum/index.php?threads/the-validator-find-errors-quickly-and-with-minimal-pain.597480/page-159#post-26262168
    #'IMPRISON_CHARACTER_INTERACTION_ACCEPT_EVENT',

	'on_battle_lost_leader',
	'on_major_battle_lost_leader',
	'on_battle_won_leader',
	'on_major_battle_won_leader',
	'on_battle_lost',
	'on_major_battle_lost',
	'on_battle_won',
	'on_major_battle_won',
	'on_combat_pulse',
	'on_siege_pulse',
	'on_vassal_accepts_religious_conversion',
	'on_mercenary_captain_replacement',
	'on_host_change',
	'on_employer_change',
	'on_combat_starting',
	'on_excommunicate_interaction',
]:
	add_action(name, root_scope='Char', from_scope='Char', from_from_scope='Char')

# root=prov
for name in [
	'on_rebel_revolt',
	'on_province_major_modifier',
	'on_outbreak',
]:
	add_action(name, root_scope='Prov')

# root=char, from=char, fromfrom=offmapPower
for name in [
	'on_offmap_governor_changed',
	'on_offmap_ruler_changed',
]:
	add_action(name, root_scope='Char', from_scope='Char', from_from_scope='OffmapPower')

# root=char, from=offmapPower
for name in [
	'on_offmap_monthly_pulse',
	'on_offmap_yearly_pulse',
	'on_offmap_policy_changed',
	'on_offmap_status_changed',
]:
	add_action(name, root_scope='Char', from_scope='OffmapPower')

# root=char, from=prov
for name in [
	'on_navy_returns_with_loot',
	'on_defect_from_rebels',
	'on_loot_province',
	'on_siege_over_winner_fort',
	'on_siege_over_winner_trade_post',
	'on_siege_won_leader_fort',
	'on_siege_won_leader_trade_post',
	'on_siege_lost_leader_fort',
	'on_siege_lost_leader_trade_post',
	'on_siege_over_loc_chars_fort',
	'on_siege_over_loc_chars_trade_post',
	'on_defect_to_rebels',
	'on_trade_post_construction_start',
	'on_fort_construction_start',
	'on_hospital_construction_start',
	'on_trade_post_construction_completed',
	'on_fort_construction_completed',
	'on_hospital_construction_completed',
]:
	add_action(name, root_scope='Char', from_scope='Prov')

# root=char, from=title, fromfrom=char
for name in [
	# http://forum.paradoxplaza.com/forum/showthread.php?597480-The-Validator-Find-errors-quickly-and-with-minimal-pain!&p=18094785&viewfull=1#post18094785
	'on_new_holder',
	'on_new_holder_inheritance',
	'on_new_holder_usurpation',
]:
	add_action(name, root_scope='Char', from_scope='Title', from_from_scope='Char')

# root=char
for name in [
	'on_childhood_pulse',
	'on_adolescence_pulse',
	'on_startup',
	'on_yearly_pulse',
	'on_bi_yearly_pulse',
	'on_five_year_pulse',
	'on_decade_pulse',
	'on_yearly_childhood_pulse',
	'on_focus_pulse',
	'on_birth',
	'on_adulthood',
	'on_adolescence',
	'on_post_birth',
	'on_death',
	'on_merc_rampage',
	'on_merc_leave',
	'on_merc_turn_coat_to',
	'on_merc_turn_coat_from',
	'on_holy_order_leave',
	'on_letter_event_message',
	'on_character_event_message',
	'on_warleader_death',
	'on_reform_religion',
	'on_become_doge',
	'on_unlanded',
	'on_create_chronicle_if_empty',
	'on_chronicle_start',
	'on_acquire_nickname',
	'on_over_vassal_limit_succession',
	'on_blood_brother_death',
	'on_ai_end_raid',
	'on_mercenary_hired',
	'on_mercenary_dismissed',
	'on_enforce_peace',
	'on_enforce_peace_start',
	'on_enforce_peace_six_vassals',
	'on_player_mercenary_income',
	'on_focus_selected',
	'on_quest_success',
	'on_quest_failure',
	'on_eu4_conversion_start',
	'on_eu4_conversion_done',
	'on_tyranny_gained_tyrant_only',
	'on_alternate_start',
	'on_bloodline_renamed',
	'on_artifact_renamed',
	'on_province_renamed',
	'on_title_renamed',
	'on_character_renamed',
	'on_unpledge_crusade_defense',
	'on_unpledge_crusade_participation',
	'on_pledge_crusade_defense',
	'on_pledge_crusade_participation',
	'on_crusade_canceled',
	'on_crusade_launches',
	'on_crusade_preparation_starts',
	'on_command_sub_unit',
	'on_command_unit',
	'on_unit_entering_province',
	'on_post_birth_stillbirth',
]:
	add_action(name, root_scope='Char')

# root=char, from=wonder_building, from_from=prov
for name in [
	'on_wonder_loot_start',
	'on_wonder_stage_loot_finish',
	'on_wonder_stage_loot_start',
	'on_wonder_restore_start',
	'on_wonder_restore_finish',
	'on_wonder_destroyed',
	'on_wonder_stage_finish',
	'on_wonder_construction_start',
]:
	add_action(name, root_scope='Char', from_scope='WonderBuilding', from_from_scope='Prov')

# root=char, from=wonder_upgrade, from_from=wonder_building, from_from_from=prov
for name in [
	'on_wonder_upgrade_destroyed',
	'on_wonder_upgrade_finish',
	'on_wonder_upgrade_start',
	'on_wonder_renamed',
]:
	add_action(name, root_scope='Char', from_scope='WonderUpgrade', from_from_scope='WonderBuilding', from_from_from_scope='Prov')

add_action('on_crusade_target_changes', root_scope='Char', from_scope='Char', from_from_scope='Title', from_from_from_scope='Char')
add_action('on_entering_port', root_scope='Unit', from_scope='Char')
add_action('on_county_religion_change', root_scope='Prov', from_scope='Religion')
add_action('on_character_convert_culture', root_scope='Char', from_scope='Char', from_from_scope='Culture')
add_action('on_character_convert_religion', root_scope='Char', from_scope='Char', from_from_scope='Religion')
add_action('on_artifact_inheritance', root_scope='Char', from_scope='Artifact', from_from_scope='Char')
add_action('on_revoke_attempted_started_war', root_scope='Char', from_scope='Char', from_from_scope='Title')
add_action('on_retract_vassal_attempted_started_war', root_scope='Char', from_scope='Char', from_from_scope='Title')
add_action('on_character_convert_secret_religion', root_scope='Char', from_scope='Religion')
add_action('on_society_failed_to_find_new_leader', root_scope='Char', from_scope='Char') # Supposedly should be root=society??
add_action('on_wonder_owner_change', root_scope='Char', from_scope='WonderBuilding', from_from_scope='Char', from_from_from_scope='Prov')
add_action('on_heresy_takeover', root_scope='Char', from_scope='Religion', from_from_scope='Religion')


EXPORT = {
	'KEY': 'on_actions',
	'VALUE': Exports
}
