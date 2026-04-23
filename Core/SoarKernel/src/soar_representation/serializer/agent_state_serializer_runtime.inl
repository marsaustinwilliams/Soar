void clear_runtime_output_links(agent* thisAgent)
{
    output_link* current = thisAgent->existing_output_links;
    while (current)
    {
        output_link* next = current->next;

        if (current->ids_in_tc)
        {
            remove_output_link_tc_info(thisAgent, current);
        }

        if (current->link_wme)
        {
            current->link_wme->output_link = NIL;
            wme_remove_ref(thisAgent, current->link_wme);
        }

        thisAgent->memoryManager->free_with_pool(MP_output_link, current);
        current = next;
    }

    thisAgent->existing_output_links = NIL;
    thisAgent->output_link_for_tc = NIL;
    thisAgent->output_link_tc_num = 0;
    thisAgent->collected_io_wmes = NIL;
    thisAgent->output_link_changed = false;
}

void reset_transient_agent_state(agent* thisAgent)
{
    cons* detached_goals = NIL;
    for (uint64_t letter_index = 0; letter_index < 26; ++letter_index)
    {
        const char letter = static_cast<char>('A' + letter_index);
        const uint64_t max_number = *thisAgent->symbolManager->get_id_counter(letter_index);
        for (uint64_t number = 1; number < max_number; ++number)
        {
            Symbol* sym = thisAgent->symbolManager->find_identifier(letter, number);
            if (!sym || !sym->id->isa_goal)
            {
                continue;
            }

            if ((sym == thisAgent->top_goal) || (sym == thisAgent->bottom_goal))
            {
                continue;
            }

            if (sym->id->higher_goal || sym->id->lower_goal)
            {
                continue;
            }

            push(thisAgent, sym, detached_goals);
        }
    }

    while (detached_goals)
    {
        cons* c = detached_goals;
        detached_goals = detached_goals->rest;
        Symbol* detached_goal = static_cast<Symbol*>(c->first);
        free_cons(thisAgent, c);

        remove_existing_context_and_descendents(thisAgent, detached_goal);
    }

    for (Symbol* goal = thisAgent->top_goal; goal != NIL; goal = goal->id->lower_goal)
    {
        slot* op_slot = goal->id->operator_slot;
        if (!op_slot)
        {
            continue;
        }

        if (!op_slot->wmes && !op_slot->all_preferences)
        {
            /* A previously-marked slot can be orphaned from the deferred removal
               list during restore cleanup. Re-queue empty operator slots so slot
               GC can release their symbol refs before init-soar leak checks. */
            op_slot->marked_for_possible_removal = false;
            mark_slot_for_possible_removal(thisAgent, op_slot);
        }
    }

    /* Some restore paths leave slots queued for deferred reclamation.
       Drain that queue before wiping transient list heads so marked slots
       (notably goal operator slots) are not orphaned with retained refs. */
    remove_garbage_slots(thisAgent);

    thisAgent->link_update_mode = UPDATE_LINKS_NORMALLY;
    thisAgent->changed_slots = NIL;
    thisAgent->context_slots_with_changed_accept_prefs = NIL;
    thisAgent->slots_for_possible_removal = NIL;
    thisAgent->disconnected_ids = NIL;
    thisAgent->ids_with_unknown_level = NIL;
    thisAgent->promoted_ids = NIL;
    clear_runtime_output_links(thisAgent);
    if (thisAgent->io_header && (thisAgent->io_header->reference_count > 0))
    {
        thisAgent->symbolManager->symbol_remove_ref(&thisAgent->io_header);
    }
    if (thisAgent->io_header_input && (thisAgent->io_header_input->reference_count > 0))
    {
        thisAgent->symbolManager->symbol_remove_ref(&thisAgent->io_header_input);
    }
    if (thisAgent->io_header_output && (thisAgent->io_header_output->reference_count > 0))
    {
        thisAgent->symbolManager->symbol_remove_ref(&thisAgent->io_header_output);
    }
    thisAgent->io_header = NIL;
    thisAgent->io_header_link = NIL;
    thisAgent->io_header_input = NIL;
    thisAgent->io_header_output = NIL;
    thisAgent->prev_top_state = NIL;
    thisAgent->ms_assertions = NIL;
    thisAgent->ms_retractions = NIL;
    thisAgent->ms_o_assertions = NIL;
    thisAgent->ms_i_assertions = NIL;
    thisAgent->postponed_assertions = NIL;
    thisAgent->nil_goal_retractions = NIL;
    thisAgent->active_level = 0;
    thisAgent->previous_active_level = 0;
    thisAgent->active_goal = NIL;
    thisAgent->previous_active_goal = NIL;
    thisAgent->highest_active_goal = NIL;
    thisAgent->highest_active_level = 0;
    thisAgent->change_level = 0;
    thisAgent->next_change_level = 0;
    thisAgent->PE_level = NIL;
    thisAgent->did_PE = false;
    thisAgent->parent_list_head = NIL;
}

void purge_restored_match_set(agent* thisAgent)
{
    while (thisAgent->postponed_assertions != NIL)
    {
        ms_change* msc = thisAgent->postponed_assertions;
        remove_from_dll(thisAgent->postponed_assertions, msc, next, prev);
        if (msc->p_node)
        {
            remove_from_dll(msc->p_node->b.p.tentative_assertions, msc, next_of_node, prev_of_node);
        }
        thisAgent->memoryManager->free_with_pool(MP_ms_change, msc);
    }

    while (thisAgent->ms_o_assertions != NIL)
    {
        ms_change* msc = thisAgent->ms_o_assertions;
        remove_from_dll(thisAgent->ms_o_assertions, msc, next, prev);
        if (msc->goal)
        {
            remove_from_dll(msc->goal->id->ms_o_assertions, msc, next_in_level, prev_in_level);
        }
        if (msc->p_node)
        {
            remove_from_dll(msc->p_node->b.p.tentative_assertions, msc, next_of_node, prev_of_node);
        }
        thisAgent->memoryManager->free_with_pool(MP_ms_change, msc);
    }

    while (thisAgent->ms_i_assertions != NIL)
    {
        ms_change* msc = thisAgent->ms_i_assertions;
        remove_from_dll(thisAgent->ms_i_assertions, msc, next, prev);
        if (msc->goal)
        {
            remove_from_dll(msc->goal->id->ms_i_assertions, msc, next_in_level, prev_in_level);
        }
        if (msc->p_node)
        {
            remove_from_dll(msc->p_node->b.p.tentative_assertions, msc, next_of_node, prev_of_node);
        }
        thisAgent->memoryManager->free_with_pool(MP_ms_change, msc);
    }

    while (thisAgent->ms_retractions != NIL)
    {
        ms_change* msc = thisAgent->ms_retractions;
        remove_from_dll(thisAgent->ms_retractions, msc, next, prev);
        if (msc->goal)
        {
            remove_from_dll(msc->goal->id->ms_retractions, msc, next_in_level, prev_in_level);
        }
        else if (thisAgent->nil_goal_retractions)
        {
            remove_from_dll(thisAgent->nil_goal_retractions, msc, next_in_level, prev_in_level);
        }
        if (msc->p_node)
        {
            remove_from_dll(msc->p_node->b.p.tentative_retractions, msc, next_of_node, prev_of_node);
        }
        thisAgent->memoryManager->free_with_pool(MP_ms_change, msc);
    }

    thisAgent->nil_goal_retractions = NIL;
    thisAgent->ms_assertions = NIL;

    for (Symbol* goal = thisAgent->top_goal; goal != NIL; goal = goal->id->lower_goal)
    {
        goal->id->ms_o_assertions = NIL;
        goal->id->ms_i_assertions = NIL;
        goal->id->ms_retractions = NIL;
    }
}

void purge_restored_ie_assertions(agent* thisAgent)
{
    while (thisAgent->ms_i_assertions != NIL)
    {
        ms_change* msc = thisAgent->ms_i_assertions;
        remove_from_dll(thisAgent->ms_i_assertions, msc, next, prev);
        if (msc->goal)
        {
            remove_from_dll(msc->goal->id->ms_i_assertions, msc, next_in_level, prev_in_level);
        }
        if (msc->p_node)
        {
            remove_from_dll(msc->p_node->b.p.tentative_assertions, msc, next_of_node, prev_of_node);
        }
        thisAgent->memoryManager->free_with_pool(MP_ms_change, msc);
    }

    for (Symbol* goal = thisAgent->top_goal; goal != NIL; goal = goal->id->lower_goal)
    {
        goal->id->ms_i_assertions = NIL;
    }
}

bool production_had_saved_firings(production* prod, const SavedProductionFiringCountMap& saved_firing_counts)
{
    if (!prod || !prod->name || !prod->name->is_string())
    {
        return false;
    }

    auto it = saved_firing_counts.find(prod->name->sc->name);
    return (it != saved_firing_counts.end()) && (it->second > 0);
}

void purge_restored_assertions_for_fired_productions(agent* thisAgent,
                                                     ms_change*& assertion_list,
                                                     const SavedProductionFiringCountMap& saved_firing_counts,
                                                     bool remove_from_operator_level_list)
{
    for (ms_change* current = assertion_list; current != NIL; )
    {
        ms_change* next = current->next;
        production* prod = (current->p_node != NIL) ? current->p_node->b.p.prod : NIL;
        if (production_had_saved_firings(prod, saved_firing_counts))
        {
            remove_from_dll(assertion_list, current, next, prev);
            if (remove_from_operator_level_list && current->goal)
            {
                remove_from_dll(current->goal->id->ms_o_assertions, current, next_in_level, prev_in_level);
            }
            if (current->p_node)
            {
                remove_from_dll(current->p_node->b.p.tentative_assertions, current, next_of_node, prev_of_node);
            }
            thisAgent->memoryManager->free_with_pool(MP_ms_change, current);
        }
        current = next;
    }
}

void purge_restored_ie_assertions_for_fired_productions(agent* thisAgent,
                                                        ms_change*& assertion_list,
                                                        const SavedProductionFiringCountMap& saved_firing_counts)
{
    for (ms_change* current = assertion_list; current != NIL; )
    {
        ms_change* next = current->next;
        production* prod = (current->p_node != NIL) ? current->p_node->b.p.prod : NIL;
        if (production_had_saved_firings(prod, saved_firing_counts))
        {
            remove_from_dll(assertion_list, current, next, prev);
            if (current->goal)
            {
                remove_from_dll(current->goal->id->ms_i_assertions, current, next_in_level, prev_in_level);
            }
            if (current->p_node)
            {
                remove_from_dll(current->p_node->b.p.tentative_assertions, current, next_of_node, prev_of_node);
            }
            thisAgent->memoryManager->free_with_pool(MP_ms_change, current);
        }
        current = next;
    }
}

void dedupe_restored_assertions(agent* thisAgent,
                                ms_change*& assertion_list,
                                RestoredAssertionSet& seen_assertions,
                                bool remove_from_operator_level_list)
{
    for (ms_change* current = assertion_list; current != NIL; )
    {
        ms_change* next = current->next;
        RestoredAssertionKey key = make_restored_assertion_key(thisAgent, current);
        if (!seen_assertions.insert(key).second)
        {
            remove_from_dll(assertion_list, current, next, prev);
            if (remove_from_operator_level_list && current->goal)
            {
                remove_from_dll(current->goal->id->ms_o_assertions, current, next_in_level, prev_in_level);
            }
            if (current->p_node)
            {
                remove_from_dll(current->p_node->b.p.tentative_assertions, current, next_of_node, prev_of_node);
            }
            thisAgent->memoryManager->free_with_pool(MP_ms_change, current);
        }
        current = next;
    }
}

void dedupe_restored_ie_assertions(agent* thisAgent,
                                   ms_change*& assertion_list,
                                   RestoredAssertionSet& seen_assertions)
{
    for (ms_change* current = assertion_list; current != NIL; )
    {
        ms_change* next = current->next;
        RestoredAssertionKey key = make_restored_assertion_key(thisAgent, current);
        if (!seen_assertions.insert(key).second)
        {
            remove_from_dll(assertion_list, current, next, prev);
            if (current->goal)
            {
                remove_from_dll(current->goal->id->ms_i_assertions, current, next_in_level, prev_in_level);
            }
            if (current->p_node)
            {
                remove_from_dll(current->p_node->b.p.tentative_assertions, current, next_of_node, prev_of_node);
            }
            thisAgent->memoryManager->free_with_pool(MP_ms_change, current);
        }
        current = next;
    }
}

void dedupe_restored_pending_assertions(agent* thisAgent)
{
    RestoredAssertionSet seen_assertions;
    dedupe_restored_ie_assertions(thisAgent, thisAgent->ms_i_assertions, seen_assertions);
    dedupe_restored_assertions(thisAgent, thisAgent->ms_o_assertions, seen_assertions, true);
    dedupe_restored_assertions(thisAgent, thisAgent->postponed_assertions, seen_assertions, false);
}

void finalize_restored_runtime_state(agent* thisAgent,
                                     top_level_phase saved_phase,
                                     bool saved_stopped,
                                     bool saved_input_cycle_flag,
                                     bool restored_pending_ie_explicit,
                                     bool restored_pending_pe_explicit,
                                     const SavedProductionFiringCountMap& saved_firing_counts)
{
    const bool preserve_pending_apply_matches =
        !saved_stopped && (thisAgent->ms_o_assertions != NIL);
    const bool preserve_pending_ie_matches =
        any_i_assertions_or_retractions_ready(thisAgent);

    if (!preserve_pending_apply_matches && !preserve_pending_ie_matches)
    {
        purge_restored_match_set(thisAgent);
    }
    else
    {
        if (!restored_pending_ie_explicit)
        {
            purge_restored_ie_assertions_for_fired_productions(thisAgent,
                                                               thisAgent->ms_i_assertions,
                                                               saved_firing_counts);
        }
        purge_restored_assertions_for_fired_productions(thisAgent,
                                                        thisAgent->postponed_assertions,
                                                        saved_firing_counts,
                                                        false);
        if (!restored_pending_pe_explicit)
        {
            purge_restored_assertions_for_fired_productions(thisAgent,
                                                            thisAgent->ms_o_assertions,
                                                            saved_firing_counts,
                                                            true);
        }
        dedupe_restored_pending_assertions(thisAgent);
    }

    thisAgent->active_goal = NIL;
    thisAgent->previous_active_goal = NIL;
    thisAgent->highest_active_goal = NIL;
    thisAgent->active_level = 0;
    thisAgent->previous_active_level = 0;
    thisAgent->highest_active_level = 0;
    thisAgent->change_level = 0;
    thisAgent->next_change_level = 0;
    thisAgent->FIRING_TYPE = (saved_phase == APPLY_PHASE) ? PE_PRODS : IE_PRODS;
    thisAgent->applyPhase = (saved_phase == APPLY_PHASE);
    thisAgent->did_PE = false;
    thisAgent->input_cycle_flag = saved_input_cycle_flag;
    thisAgent->current_phase = saved_phase;
}

void clear_restored_slot_change_tracking(agent* thisAgent)
{
    while (thisAgent->changed_slots)
    {
        dl_cons* dc = thisAgent->changed_slots;
        thisAgent->changed_slots = dc->next;
        if (dc->item)
        {
            static_cast<slot_struct*>(dc->item)->changed = NIL;
        }
        thisAgent->memoryManager->free_with_pool(MP_dl_cons, dc);
    }

    while (thisAgent->context_slots_with_changed_accept_prefs)
    {
        dl_cons* dc = thisAgent->context_slots_with_changed_accept_prefs;
        thisAgent->context_slots_with_changed_accept_prefs = dc->next;
        if (dc->item)
        {
            static_cast<slot_struct*>(dc->item)->acceptable_preference_changed = NIL;
        }
        thisAgent->memoryManager->free_with_pool(MP_dl_cons, dc);
    }

    for (Symbol* goal = thisAgent->top_goal; goal != NIL; goal = goal->id->lower_goal)
    {
        if (goal->id->operator_slot)
        {
            goal->id->operator_slot->changed = NIL;
            goal->id->operator_slot->acceptable_preference_changed = NIL;
        }
    }
}

Symbol* highest_restored_apply_activity_goal(agent* thisAgent)
{
    for (Symbol* goal = thisAgent->top_goal; goal != NIL; goal = goal->id->lower_goal)
    {
        if (goal->id->ms_i_assertions || goal->id->ms_o_assertions || goal->id->ms_retractions)
        {
            return goal;
        }
    }

    if (thisAgent->nil_goal_retractions)
    {
        return NIL;
    }

    return NIL;
}

void rebuild_restored_context_slot_change_tracking(agent* thisAgent,
                                                   top_level_phase saved_phase)
{
    if (saved_phase != APPLY_PHASE)
    {
        return;
    }

    Symbol* pending_activity_goal = highest_restored_apply_activity_goal(thisAgent);
    if (!pending_activity_goal || !pending_activity_goal->id->operator_slot)
    {
        return;
    }

    mark_slot_as_changed(thisAgent, pending_activity_goal->id->operator_slot);
}

void release_internal_wme_reference(agent* thisAgent, wme*& candidate)
{
    if (!thisAgent || !candidate)
    {
        return;
    }

    if (wme_is_in_rete(thisAgent, candidate))
    {
        remove_wme_from_wm(thisAgent, candidate);
    }
    else if (candidate->reference_count > 0)
    {
        wme_remove_ref(thisAgent, candidate);
    }

    candidate = NIL;
}

void release_identifier_rl_runtime_state(Symbol* sym)
{
    if (!sym || !sym->is_sti() || !sym->id->rl_info)
    {
        return;
    }

    agent* thisAgent = sym->id->thisAgent;
    rl_data* rl_info = sym->id->rl_info;

    release_internal_wme_reference(thisAgent, rl_info->rl_link_wme);

    if (rl_info->prev_op_rl_rules)
    {
        for (production* prod : *rl_info->prev_op_rl_rules)
        {
            if (prod && (prod->rl_ref_count > 0))
            {
                prod->rl_ref_count--;
            }
        }
        rl_info->prev_op_rl_rules->~production_list();
        thisAgent->memoryManager->free_with_pool(MP_rl_rule, rl_info->prev_op_rl_rules);
        rl_info->prev_op_rl_rules = NIL;
    }

    if (rl_info->eligibility_traces)
    {
        rl_info->eligibility_traces->clear();
        rl_info->eligibility_traces->~rl_et_map();
        thisAgent->memoryManager->free_with_pool(MP_rl_et, rl_info->eligibility_traces);
        rl_info->eligibility_traces = NIL;
    }

    thisAgent->memoryManager->free_with_pool(MP_rl_info, rl_info);
    sym->id->rl_info = NIL;
}

void release_identifier_epmem_runtime_state(Symbol* sym)
{
    if (!sym || !sym->is_sti() || !sym->id->epmem_info)
    {
        return;
    }

    agent* thisAgent = sym->id->thisAgent;
    epmem_data* epmem_info = sym->id->epmem_info;

    release_internal_wme_reference(thisAgent, epmem_info->epmem_link_wme);
    release_internal_wme_reference(thisAgent, epmem_info->cmd_wme);
    release_internal_wme_reference(thisAgent, epmem_info->result_wme);
    release_internal_wme_reference(thisAgent, epmem_info->epmem_time_wme);

    if (epmem_info->epmem_wmes)
    {
        epmem_info->epmem_wmes->~preference_list();
        thisAgent->memoryManager->free_with_pool(MP_epmem_wmes, epmem_info->epmem_wmes);
        epmem_info->epmem_wmes = NIL;
    }

    thisAgent->memoryManager->free_with_pool(MP_epmem_info, epmem_info);
    sym->id->epmem_info = NIL;
}

void release_identifier_smem_runtime_state(Symbol* sym)
{
    if (!sym || !sym->is_sti() || !sym->id->smem_info)
    {
        return;
    }

    agent* thisAgent = sym->id->thisAgent;
    smem_data* smem_info = sym->id->smem_info;

    release_internal_wme_reference(thisAgent, smem_info->smem_link_wme);
    release_internal_wme_reference(thisAgent, smem_info->cmd_wme);
    release_internal_wme_reference(thisAgent, smem_info->result_wme);

    if (smem_info->smem_wmes)
    {
        smem_info->smem_wmes->~preference_list();
        thisAgent->memoryManager->free_with_pool(MP_smem_wmes, smem_info->smem_wmes);
        smem_info->smem_wmes = NIL;
    }

    thisAgent->memoryManager->free_with_pool(MP_smem_info, smem_info);
    sym->id->smem_info = NIL;
}

bool goal_is_on_current_chain(agent* thisAgent, Symbol* goal)
{
    for (Symbol* current = thisAgent->top_goal; current != NIL; current = current->id->lower_goal)
    {
        if (current == goal)
        {
            return true;
        }
    }
    return false;
}

void purge_orphan_goal_runtime_state(agent* thisAgent)
{
    const bool serializer_trace = (std::getenv("SOAR_DEBUG_SERIALIZER_TRACE") != nullptr);
    for (uint64_t letter_index = 0; letter_index < 26; ++letter_index)
    {
        const char letter = static_cast<char>('A' + letter_index);
        const uint64_t max_number = *thisAgent->symbolManager->get_id_counter(letter_index);
        if (serializer_trace && (letter == 'S'))
        {
            std::cerr << "[SERIALIZER_TRACE] purge_orphan S max_number=" << max_number << std::endl;
        }

        for (uint64_t number = 1; number < max_number; ++number)
        {
            Symbol* sym = thisAgent->symbolManager->find_identifier(letter, number);
            if (!sym || !sym->is_sti() || !sym->id->isa_goal)
            {
                continue;
            }

            if (serializer_trace &&
                ((letter == 'S' && number == 1) ||
                 (letter == 'J' && number == 1) ||
                 (letter == 'I' && number == 4) ||
                 (letter == 'O' && number == 2) ||
                 (letter == 'O' && number == 3)))
            {
                std::cerr << "[SERIALIZER_TRACE] purge_orphan candidate "
                          << letter << number
                          << " on_chain=" << (goal_is_on_current_chain(thisAgent, sym) ? 1 : 0)
                          << " ref=" << sym->reference_count
                          << std::endl;
            }

            if (goal_is_on_current_chain(thisAgent, sym))
            {
                continue;
            }

            release_identifier_rl_runtime_state(sym);
            release_identifier_epmem_runtime_state(sym);
            release_identifier_smem_runtime_state(sym);

            if (sym->id->operator_slot &&
                !sym->id->operator_slot->wmes &&
                !sym->id->operator_slot->all_preferences)
            {
                sym->id->operator_slot->marked_for_possible_removal = false;
                mark_slot_for_possible_removal(thisAgent, sym->id->operator_slot);
            }

            sym->id->isa_goal = false;
            sym->id->higher_goal = NIL;
            sym->id->lower_goal = NIL;
            sym->id->preferences_from_goal = NIL;
            if (serializer_trace)
            {
                std::cerr << "[SERIALIZER_TRACE] purge_orphan released "
                          << letter << number << std::endl;
            }
        }
    }

    remove_garbage_slots(thisAgent);
}

void reset_restored_identifier_runtime_state(Symbol* sym)
{
    if (!sym || !sym->is_sti())
    {
        return;
    }

    agent* thisAgent = sym->id->thisAgent;

    release_identifier_rl_runtime_state(sym);
    release_identifier_epmem_runtime_state(sym);
    release_identifier_smem_runtime_state(sym);

    sym->decider_flag = 0;
    sym->decider_wme = NIL;
    sym->retesave_symindex = 0;
    sym->tc_num = 0;
    sym->epmem_valid = NIL;
    sym->smem_valid = NIL;

    if (thisAgent && sym->id->slots)
    {
        slot* stale_slot = sym->id->slots;
        while (stale_slot)
        {
            slot* next_slot = stale_slot->next;
            if (stale_slot->isa_context_slot)
            {
                remove_wmes_for_context_slot(thisAgent, stale_slot);
            }
            stale_slot->wmes = NIL;
            stale_slot->all_preferences = NIL;
            stale_slot->acceptable_preference_wmes = NIL;
            stale_slot->changed = NIL;
            stale_slot->acceptable_preference_changed = NIL;
            mark_slot_for_possible_removal(thisAgent, stale_slot);
            stale_slot = next_slot;
        }
        remove_garbage_slots(thisAgent);
    }

    sym->id->isa_goal = false;
    sym->id->isa_impasse = false;
    sym->id->impasse_type = NONE_IMPASSE_TYPE;
    sym->id->did_PE = false;
    sym->id->isa_operator = 0;
    sym->id->allow_bottom_up_chunks = true;
    sym->id->could_be_a_link_from_below = false;
    sym->id->link_count = 0;
    sym->id->unknown_level = NIL;
    sym->id->slots = NIL;
    sym->id->impasse_wmes = NIL;
    sym->id->higher_goal = NIL;
    sym->id->lower_goal = NIL;
    sym->id->operator_slot = NIL;
    sym->id->preferences_from_goal = NIL;
    sym->id->rl_info = NIL;
    sym->id->epmem_info = NIL;
    sym->id->epmem_id = EPMEM_NODEID_BAD;
    sym->id->smem_info = NIL;
    sym->id->LTI_ID = NIL;
    sym->id->LTI_epmem_valid = NIL;
    sym->id->gds = NIL;
    sym->id->saved_firing_type = NO_SAVED_PRODS;
    sym->id->ms_o_assertions = NIL;
    sym->id->ms_i_assertions = NIL;
    sym->id->ms_retractions = NIL;
    sym->id->associated_output_links = NIL;
    sym->id->input_wmes = NIL;
    sym->id->depth = 0;
    sym->id->rl_trace = NIL;
}

soar::kernel::SymbolType symbol_to_proto_type(Symbol* sym)
{
    if (!sym) return soar::kernel::SYMBOL_TYPE_UNKNOWN;
    if (sym->is_variable()) return soar::kernel::SYMBOL_TYPE_VARIABLE;
    if (sym->is_sti()) return soar::kernel::SYMBOL_TYPE_IDENTIFIER;
    if (sym->is_string()) return soar::kernel::SYMBOL_TYPE_STRING;
    if (sym->is_int()) return soar::kernel::SYMBOL_TYPE_INT;
    if (sym->is_float()) return soar::kernel::SYMBOL_TYPE_FLOAT;
    return soar::kernel::SYMBOL_TYPE_UNKNOWN;
}
