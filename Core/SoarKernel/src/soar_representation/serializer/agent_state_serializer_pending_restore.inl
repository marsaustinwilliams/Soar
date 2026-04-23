preference* find_matching_slot_preference(agent* thisAgent,
                                          Symbol* id,
                                          Symbol* attr,
                                          Symbol* value,
                                          uint64_t identity_id,
                                          uint64_t identity_attr,
                                          uint64_t identity_value)
{
    slot* s = find_slot(id, attr);
    if (!s)
    {
        return NIL;
    }

    preference* best_match = NIL;
    for (preference* slot_pref = s->all_preferences; slot_pref != NIL; slot_pref = slot_pref->all_of_slot_next)
    {
        if (id && attr && value && id->is_sti() && attr->is_constant() &&
            (id->to_string(true, false, NIL, 0) == std::string("S2")) &&
            (attr->to_string(true, false, NIL, 0) == std::string("operator")))
        {
            std::fprintf(stderr,
                         "repair_slot_candidate: type=%d value=%s referent=%s inst=%s slot=%u ids=%llu/%llu/%llu\n",
                         static_cast<int>(slot_pref->type),
                         slot_pref->value ? slot_pref->value->to_string(true, false, NIL, 0) : "<nil>",
                         slot_pref->referent ? slot_pref->referent->to_string(true, false, NIL, 0) : "<nil>",
                         (slot_pref->inst && slot_pref->inst->prod_name && slot_pref->inst->prod_name->is_string()) ? slot_pref->inst->prod_name->sc->name : "<arch>",
                         slot_pref->slot ? 1U : 0U,
                         static_cast<unsigned long long>(slot_pref->identities.id ? slot_pref->identities.id->get_sub_identity() : 0),
                         static_cast<unsigned long long>(slot_pref->identities.attr ? slot_pref->identities.attr->get_sub_identity() : 0),
                         static_cast<unsigned long long>(slot_pref->identities.value ? slot_pref->identities.value->get_sub_identity() : 0));
        }

        if ((slot_pref->id != id) || (slot_pref->attr != attr) || (slot_pref->value != value) || !slot_pref->slot)
        {
            continue;
        }

        if (identity_id && slot_pref->identities.id &&
            (slot_pref->identities.id->get_sub_identity() != identity_id))
        {
            continue;
        }
        if (identity_attr && slot_pref->identities.attr &&
            (slot_pref->identities.attr->get_sub_identity() != identity_attr))
        {
            continue;
        }
        if (identity_value && slot_pref->identities.value &&
            (slot_pref->identities.value->get_sub_identity() != identity_value))
        {
            continue;
        }

        best_match = slot_pref;
        if (slot_pref->inst && slot_pref->inst->prod_name != thisAgent->symbolManager->soarSymbols.architecture_inst_symbol)
        {
            break;
        }
    }

    return best_match;
}

preference* find_matching_restored_preference(agent* thisAgent,
                                              const RestoredPreferenceMap& preference_map,
                                              Symbol* id,
                                              Symbol* attr,
                                              Symbol* value,
                                              uint64_t identity_id,
                                              uint64_t identity_attr,
                                              uint64_t identity_value)
{
    preference* best_match = NIL;
    int best_score = -1;

    for (const auto& entry : preference_map)
    {
        preference* pref = entry.second;
        if (id && attr && value && id->is_sti() && attr->is_constant() && value->is_sti() &&
            (id->to_string(true, false, NIL, 0) == std::string("S2")) &&
            (attr->to_string(true, false, NIL, 0) == std::string("operator")) &&
            (value->to_string(true, false, NIL, 0) == std::string("O1")) && pref)
        {
            std::fprintf(stderr,
                         "repair_prefmap_candidate: type=%d id=%s attr=%s value=%s referent=%s inst=%s slot=%u ids=%llu/%llu/%llu\n",
                         static_cast<int>(pref->type),
                         pref->id ? pref->id->to_string(true, false, NIL, 0) : "<nil>",
                         pref->attr ? pref->attr->to_string(true, false, NIL, 0) : "<nil>",
                         pref->value ? pref->value->to_string(true, false, NIL, 0) : "<nil>",
                         pref->referent ? pref->referent->to_string(true, false, NIL, 0) : "<nil>",
                         (pref->inst && pref->inst->prod_name && pref->inst->prod_name->is_string()) ? pref->inst->prod_name->sc->name : "<arch>",
                         pref->slot ? 1U : 0U,
                         static_cast<unsigned long long>(pref->identities.id ? pref->identities.id->get_sub_identity() : 0),
                         static_cast<unsigned long long>(pref->identities.attr ? pref->identities.attr->get_sub_identity() : 0),
                         static_cast<unsigned long long>(pref->identities.value ? pref->identities.value->get_sub_identity() : 0));
        }

        if (!pref || (pref->id != id) || (pref->attr != attr) || ((pref->value != value) && (pref->referent != value)))
        {
            continue;
        }

        if (identity_id && pref->identities.id && (pref->identities.id->get_sub_identity() != identity_id))
        {
            continue;
        }
        if (identity_attr && pref->identities.attr && (pref->identities.attr->get_sub_identity() != identity_attr))
        {
            continue;
        }
        if (identity_value && pref->identities.value && (pref->identities.value->get_sub_identity() != identity_value))
        {
            continue;
        }

        int score = 0;
        if (pref->inst && (pref->inst->prod_name != thisAgent->symbolManager->soarSymbols.architecture_inst_symbol))
        {
            score += 2;
        }
        if (pref->slot)
        {
            score += 2;
        }
        if (pref->type != ::ACCEPTABLE_PREFERENCE_TYPE)
        {
            score += 1;
        }

        if (score > best_score)
        {
            best_score = score;
            best_match = pref;
        }
    }

    return best_match;
}

preference* find_matching_live_instantiation_preference(agent* thisAgent,
                                                        Symbol* id,
                                                        Symbol* attr,
                                                        Symbol* value,
                                                        uint64_t identity_id,
                                                        uint64_t identity_attr,
                                                        uint64_t identity_value)
{
    preference* best_match = NIL;
    int best_score = -1;

    for (int production_type = 0; production_type < NUM_PRODUCTION_TYPES; ++production_type)
    {
        for (production* prod = thisAgent->all_productions_of_type[production_type]; prod != NIL; prod = prod->next)
        {
            for (instantiation* inst = prod->instantiations; inst != NIL; inst = inst->next)
            {
                for (preference* pref = inst->preferences_generated; pref != NIL; pref = pref->inst_next)
                {
                    if (id && attr && value && id->is_sti() && attr->is_constant() && value->is_sti() &&
                        (id->to_string(true, false, NIL, 0) == std::string("S2")) &&
                        (attr->to_string(true, false, NIL, 0) == std::string("operator")) &&
                        (value->to_string(true, false, NIL, 0) == std::string("O1")))
                    {
                        std::fprintf(stderr,
                                     "repair_livepref_candidate: prod=%s type=%d value=%s referent=%s slot=%u ids=%llu/%llu/%llu\n",
                                     (pref->inst && pref->inst->prod_name && pref->inst->prod_name->is_string()) ? pref->inst->prod_name->sc->name : "<arch>",
                                     static_cast<int>(pref->type),
                                     pref->value ? pref->value->to_string(true, false, NIL, 0) : "<nil>",
                                     pref->referent ? pref->referent->to_string(true, false, NIL, 0) : "<nil>",
                                     pref->slot ? 1U : 0U,
                                     static_cast<unsigned long long>(pref->identities.id ? pref->identities.id->get_sub_identity() : 0),
                                     static_cast<unsigned long long>(pref->identities.attr ? pref->identities.attr->get_sub_identity() : 0),
                                     static_cast<unsigned long long>(pref->identities.value ? pref->identities.value->get_sub_identity() : 0));
                    }

                    if ((pref->id != id) || (pref->attr != attr) || ((pref->value != value) && (pref->referent != value)))
                    {
                        continue;
                    }

                    if (identity_id && pref->identities.id && (pref->identities.id->get_sub_identity() != identity_id))
                    {
                        continue;
                    }
                    if (identity_attr && pref->identities.attr && (pref->identities.attr->get_sub_identity() != identity_attr))
                    {
                        continue;
                    }
                    if (identity_value && pref->identities.value && (pref->identities.value->get_sub_identity() != identity_value))
                    {
                        continue;
                    }

                    int score = 0;
                    if (pref->inst && (pref->inst->prod_name != thisAgent->symbolManager->soarSymbols.architecture_inst_symbol))
                    {
                        score += 2;
                    }
                    if (pref->slot)
                    {
                        score += 2;
                    }
                    if (pref->type != ::ACCEPTABLE_PREFERENCE_TYPE)
                    {
                        score += 1;
                    }

                    if (score > best_score)
                    {
                        best_score = score;
                        best_match = pref;
                    }
                }
            }
        }
    }

    return best_match;
}

void repair_synthetic_saved_condition_traces(agent* thisAgent,
                                             instantiation* inst,
                                             const RestoredPreferenceMap& preference_map)
{
    if (!inst)
    {
        return;
    }

    for (condition* cond = inst->top_of_instantiated_conditions; cond != NIL; cond = cond->next)
    {
        if ((cond->type != POSITIVE_CONDITION) || !cond->data.tests.id_test || !cond->data.tests.attr_test || !cond->data.tests.value_test)
        {
            continue;
        }

        test id_eq = cond->data.tests.id_test->eq_test;
        test attr_eq = cond->data.tests.attr_test->eq_test;
        test value_eq = cond->data.tests.value_test->eq_test;
        if (!id_eq || !attr_eq || !value_eq)
        {
            continue;
        }

        preference* trace_pref = cond->bt.trace;
        if (trace_pref && trace_pref->slot && trace_pref->inst &&
            (trace_pref->inst->prod_name != thisAgent->symbolManager->soarSymbols.architecture_inst_symbol))
        {
            continue;
        }

        preference* repaired_pref = find_matching_live_instantiation_preference(thisAgent,
                                                                                id_eq->data.referent,
                                                                                attr_eq->data.referent,
                                                                                value_eq->data.referent,
                                                                                id_eq->identity ? id_eq->identity->get_sub_identity() : 0,
                                                                                attr_eq->identity ? attr_eq->identity->get_sub_identity() : 0,
                                                                                value_eq->identity ? value_eq->identity->get_sub_identity() : 0);
        if (!repaired_pref)
        {
            repaired_pref = find_matching_restored_preference(thisAgent,
                                                              preference_map,
                                                              id_eq->data.referent,
                                                              attr_eq->data.referent,
                                                              value_eq->data.referent,
                                                              id_eq->identity ? id_eq->identity->get_sub_identity() : 0,
                                                              attr_eq->identity ? attr_eq->identity->get_sub_identity() : 0,
                                                              value_eq->identity ? value_eq->identity->get_sub_identity() : 0);
        }
        if (!repaired_pref)
        {
            repaired_pref = find_matching_slot_preference(thisAgent,
                                                          id_eq->data.referent,
                                                          attr_eq->data.referent,
                                                          value_eq->data.referent,
                                                          id_eq->identity ? id_eq->identity->get_sub_identity() : 0,
                                                          attr_eq->identity ? attr_eq->identity->get_sub_identity() : 0,
                                                          value_eq->identity ? value_eq->identity->get_sub_identity() : 0);
        }
        if (!repaired_pref)
        {
            continue;
        }

        if (serializer_should_manage_cond_trace_refs(inst->match_goal_level))
        {
            /* Only add-ref the newly repaired preference. The old cond->bt.trace on
             * synthetic instantiations was assigned directly from wme->preference
             * by create_refracted_instantiation without an add_ref, so we must NOT
             * remove_ref it here; doing so would decrement a ref we never took
             * (and crash if the preference was already freed by init-soar). */
            preference_add_ref(repaired_pref);
        }
        cond->bt.trace = repaired_pref;
    }

    if (inst->OSK_prefs)
    {
        clear_preference_list(thisAgent, inst->OSK_prefs);
        inst->OSK_prefs = NIL;
    }

    /* Leave synthetic instantiation OSK reconstruction to
     * refresh_synthetic_instantiation_osk_from_slots(), which
     * repopulates from slot-owned OSK lists after trace repair. */
}

production* find_restored_production_by_name(agent* thisAgent, Symbol* prod_name)
{
    if (!prod_name || !prod_name->is_string())
    {
        return NIL;
    }

    for (int production_type = 0; production_type < NUM_PRODUCTION_TYPES; ++production_type)
    {
        for (production* current = thisAgent->all_productions_of_type[production_type]; current != NIL; current = current->next)
        {
            if (current->name && current->name->is_string() && (std::strcmp(current->name->sc->name, prod_name->sc->name) == 0))
            {
                return current;
            }
        }
    }

    return NIL;
}

bool pending_change_exists(ms_change* head, rete_node* p_node, token* tok, wme* w)
{
    for (ms_change* current = head; current != NIL; current = current->next)
    {
        if ((current->p_node == p_node) && (current->tok == tok) && (current->w == w))
        {
            return true;
        }
    }

    return false;
}

bool pending_change_exists_in_node(ms_change* head, token* tok, wme* w)
{
    for (ms_change* current = head; current != NIL; current = current->next_of_node)
    {
        if ((current->tok == tok) && (current->w == w))
        {
            return true;
        }
    }

    return false;
}

void restore_pending_ie_assertions(agent* thisAgent,
                                   const google::protobuf::RepeatedPtrField<soar::kernel::PendingIeAssertion>& pending_ie_assertions,
                                   const SymbolReverseMap& symbol_map)
{
    for (const auto& pending_assertion : pending_ie_assertions)
    {
        Symbol* prod_name = symbol_by_id(symbol_map, pending_assertion.production_name_symbol());
        goal_stack_level match_level = static_cast<goal_stack_level>(pending_assertion.match_goal_level());

        production* prod = find_restored_production_by_name(thisAgent, prod_name);
        if (!prod || !prod->p_node)
        {
            continue;
        }

        std::vector<uint64_t> timetags;
        timetags.reserve(static_cast<size_t>(pending_assertion.timetags_size()));
        for (int timetag_index = 0; timetag_index < pending_assertion.timetags_size(); ++timetag_index)
        {
            timetags.push_back(pending_assertion.timetags(timetag_index));
        }

        bool restored = false;
        for (token* pnode_tok = prod->p_node->a.np.tokens; pnode_tok != NIL; pnode_tok = pnode_tok->next_of_node)
        {
            token* match_tok = pnode_tok->parent;
            wme* match_wme = pnode_tok->w;

            if (!match_tok || !match_timetags_equal(thisAgent, match_tok, match_wme, timetags))
            {
                continue;
            }

            ms_change temp_msc;
            temp_msc.tok = match_tok;
            temp_msc.w = match_wme;
            temp_msc.p_node = prod->p_node;

            Symbol* match_goal = goal_for_level(thisAgent, match_level);
            if (!match_goal)
            {
                match_goal = find_goal_for_match_set_change_assertion(thisAgent, &temp_msc);
            }
            if (!match_goal)
            {
                break;
            }

            if (pending_change_exists(thisAgent->ms_i_assertions, prod->p_node, match_tok, match_wme) ||
                pending_change_exists(thisAgent->ms_o_assertions, prod->p_node, match_tok, match_wme) ||
                pending_change_exists(thisAgent->postponed_assertions, prod->p_node, match_tok, match_wme) ||
                pending_change_exists_in_node(prod->p_node->b.p.tentative_assertions, match_tok, match_wme))
            {
                restored = true;
                break;
            }

            ms_change* restored_change = NIL;
            thisAgent->memoryManager->allocate_with_pool(MP_ms_change, &restored_change);
            restored_change->p_node = prod->p_node;
            restored_change->tok = match_tok;
            restored_change->w = match_wme;
            restored_change->inst = NIL;
            restored_change->goal = match_goal;
            restored_change->level = match_goal->id->level;

            insert_at_head_of_dll(thisAgent->ms_i_assertions, restored_change, next, prev);
            insert_at_head_of_dll(match_goal->id->ms_i_assertions, restored_change, next_in_level, prev_in_level);
            insert_at_head_of_dll(prod->p_node->b.p.tentative_assertions, restored_change, next_of_node, prev_of_node);
            prod->OPERAND_which_assert_list = I_LIST;
            restored = true;
            break;
        }

        if (restored)
        {
            continue;
        }

        for (token* pnode_tok = prod->p_node->a.np.tokens; pnode_tok != NIL; pnode_tok = pnode_tok->next_of_node)
        {
            token* match_tok = pnode_tok->parent;
            wme* match_wme = pnode_tok->w;
            if (!match_tok)
            {
                continue;
            }

            ms_change temp_msc;
            temp_msc.tok = match_tok;
            temp_msc.w = match_wme;
            temp_msc.p_node = prod->p_node;

            Symbol* match_goal = goal_for_level(thisAgent, match_level);
            if (!match_goal)
            {
                match_goal = find_goal_for_match_set_change_assertion(thisAgent, &temp_msc);
            }
            if (!match_goal || (match_goal->id->level != match_level))
            {
                continue;
            }

            if (pending_change_exists(thisAgent->ms_i_assertions, prod->p_node, match_tok, match_wme) ||
                pending_change_exists(thisAgent->ms_o_assertions, prod->p_node, match_tok, match_wme) ||
                pending_change_exists(thisAgent->postponed_assertions, prod->p_node, match_tok, match_wme) ||
                pending_change_exists_in_node(prod->p_node->b.p.tentative_assertions, match_tok, match_wme))
            {
                restored = true;
                break;
            }

            ms_change* restored_change = NIL;
            thisAgent->memoryManager->allocate_with_pool(MP_ms_change, &restored_change);
            restored_change->p_node = prod->p_node;
            restored_change->tok = match_tok;
            restored_change->w = match_wme;
            restored_change->inst = NIL;
            restored_change->goal = match_goal;
            restored_change->level = match_goal->id->level;

            insert_at_head_of_dll(thisAgent->ms_i_assertions, restored_change, next, prev);
            insert_at_head_of_dll(match_goal->id->ms_i_assertions, restored_change, next_in_level, prev_in_level);
            insert_at_head_of_dll(prod->p_node->b.p.tentative_assertions, restored_change, next_of_node, prev_of_node);
            prod->OPERAND_which_assert_list = I_LIST;
            break;
        }
    }
}

void restore_pending_pe_assertions(agent* thisAgent,
                                   const google::protobuf::RepeatedPtrField<soar::kernel::PendingIeAssertion>& pending_pe_assertions,
                                   const SymbolReverseMap& symbol_map)
{
    for (const auto& pending_assertion : pending_pe_assertions)
    {
        Symbol* prod_name = symbol_by_id(symbol_map, pending_assertion.production_name_symbol());
        goal_stack_level match_level = static_cast<goal_stack_level>(pending_assertion.match_goal_level());

        production* prod = find_restored_production_by_name(thisAgent, prod_name);
        if (!prod || !prod->p_node)
        {
            continue;
        }

        const bool debug_make_chunk = prod->name && prod->name->is_string() && (strcmp(prod->name->sc->name, "make-chunk") == 0);

        std::vector<uint64_t> timetags;
        timetags.reserve(static_cast<size_t>(pending_assertion.timetags_size()));
        for (int timetag_index = 0; timetag_index < pending_assertion.timetags_size(); ++timetag_index)
        {
            timetags.push_back(pending_assertion.timetags(timetag_index));
        }

        bool restored = false;
        for (token* pnode_tok = prod->p_node->a.np.tokens; pnode_tok != NIL; pnode_tok = pnode_tok->next_of_node)
        {
            token* match_tok = pnode_tok->parent;
            wme* match_wme = pnode_tok->w;

            if (!match_tok || !match_timetags_equal(thisAgent, match_tok, match_wme, timetags))
            {
                continue;
            }

            ms_change temp_msc;
            temp_msc.tok = match_tok;
            temp_msc.w = match_wme;
            temp_msc.p_node = prod->p_node;

            Symbol* match_goal = goal_for_level(thisAgent, match_level);
            if (!match_goal)
            {
                match_goal = find_goal_for_match_set_change_assertion(thisAgent, &temp_msc);
            }
            if (!match_goal)
            {
                break;
            }

            if (pending_change_exists(thisAgent->ms_i_assertions, prod->p_node, match_tok, match_wme) ||
                pending_change_exists(thisAgent->ms_o_assertions, prod->p_node, match_tok, match_wme) ||
                pending_change_exists(thisAgent->postponed_assertions, prod->p_node, match_tok, match_wme) ||
                pending_change_exists_in_node(prod->p_node->b.p.tentative_assertions, match_tok, match_wme))
            {
                restored = true;
                break;
            }

            ms_change* restored_change = NIL;
            thisAgent->memoryManager->allocate_with_pool(MP_ms_change, &restored_change);
            restored_change->p_node = prod->p_node;
            restored_change->tok = match_tok;
            restored_change->w = match_wme;
            restored_change->inst = NIL;
            restored_change->goal = match_goal;
            restored_change->level = match_goal->id->level;

            insert_at_head_of_dll(thisAgent->ms_o_assertions, restored_change, next, prev);
            insert_at_head_of_dll(match_goal->id->ms_o_assertions, restored_change, next_in_level, prev_in_level);
            insert_at_head_of_dll(prod->p_node->b.p.tentative_assertions, restored_change, next_of_node, prev_of_node);
            prod->OPERAND_which_assert_list = O_LIST;
            if (debug_make_chunk)
            {
                thisAgent->outputManager->printa_sf(thisAgent,
                                                    "restore_pending_pe_assertions: exact make-chunk match restored at goal level %d using %u timetags\n",
                                                    static_cast<int>(match_goal->id->level),
                                                    static_cast<uint64_t>(timetags.size()));
            }
            restored = true;
            break;
        }

        if (restored)
        {
            continue;
        }

        for (token* pnode_tok = prod->p_node->a.np.tokens; pnode_tok != NIL; pnode_tok = pnode_tok->next_of_node)
        {
            token* match_tok = pnode_tok->parent;
            wme* match_wme = pnode_tok->w;
            if (!match_tok)
            {
                continue;
            }

            ms_change temp_msc;
            temp_msc.tok = match_tok;
            temp_msc.w = match_wme;
            temp_msc.p_node = prod->p_node;

            Symbol* match_goal = goal_for_level(thisAgent, match_level);
            if (!match_goal)
            {
                match_goal = find_goal_for_match_set_change_assertion(thisAgent, &temp_msc);
            }
            if (!match_goal || (match_goal->id->level != match_level))
            {
                continue;
            }

            if (pending_change_exists(thisAgent->ms_i_assertions, prod->p_node, match_tok, match_wme) ||
                pending_change_exists(thisAgent->ms_o_assertions, prod->p_node, match_tok, match_wme) ||
                pending_change_exists(thisAgent->postponed_assertions, prod->p_node, match_tok, match_wme) ||
                pending_change_exists_in_node(prod->p_node->b.p.tentative_assertions, match_tok, match_wme))
            {
                restored = true;
                break;
            }

            ms_change* restored_change = NIL;
            thisAgent->memoryManager->allocate_with_pool(MP_ms_change, &restored_change);
            restored_change->p_node = prod->p_node;
            restored_change->tok = match_tok;
            restored_change->w = match_wme;
            restored_change->inst = NIL;
            restored_change->goal = match_goal;
            restored_change->level = match_goal->id->level;

            insert_at_head_of_dll(thisAgent->ms_o_assertions, restored_change, next, prev);
            insert_at_head_of_dll(match_goal->id->ms_o_assertions, restored_change, next_in_level, prev_in_level);
            insert_at_head_of_dll(prod->p_node->b.p.tentative_assertions, restored_change, next_of_node, prev_of_node);
            prod->OPERAND_which_assert_list = O_LIST;
            if (debug_make_chunk)
            {
                thisAgent->outputManager->printa_sf(thisAgent,
                                                    "restore_pending_pe_assertions: fallback make-chunk match restored at goal level %d\n",
                                                    static_cast<int>(match_goal->id->level));
            }
            break;
        }
    }
}

void restore_pending_ie_assertions(agent* thisAgent,
                                   const soar::kernel::AgentState& state,
                                   const SymbolReverseMap& symbol_map)
{
    const auto& trace_settings = state.settings().trace_settings();
    const int extension_index = HIGHEST_SYSPARAM_NUMBER + 1;
    for (int index = extension_index; index + 2 < trace_settings.size(); ++index)
    {
        if ((trace_settings.Get(index) != kSnapshotPendingIeAssertionsMagic) ||
            (trace_settings.Get(index + 1) != kSnapshotPendingIeAssertionsVersion))
        {
            continue;
        }

        index += 2;
        const int64_t count = trace_settings.Get(index++);
        int64_t restored_count = 0;
        for (int64_t assertion_index = 0; assertion_index < count && index < trace_settings.size(); ++assertion_index)
        {
            if (index + 2 >= trace_settings.size())
            {
                return;
            }

            Symbol* prod_name = symbol_by_id(symbol_map, static_cast<uint64_t>(trace_settings.Get(index++)));
            goal_stack_level match_level = static_cast<goal_stack_level>(trace_settings.Get(index++));
            int64_t timetag_count = trace_settings.Get(index++);
            if (timetag_count < 0 || (index + timetag_count) > trace_settings.size())
            {
                return;
            }

            std::vector<uint64_t> timetags;
            timetags.reserve(static_cast<size_t>(timetag_count));
            for (int64_t timetag_index = 0; timetag_index < timetag_count; ++timetag_index)
            {
                timetags.push_back(static_cast<uint64_t>(trace_settings.Get(index++)));
            }

            production* prod = find_restored_production_by_name(thisAgent, prod_name);
            if (!prod || !prod->p_node)
            {
                continue;
            }

            for (token* pnode_tok = prod->p_node->a.np.tokens; pnode_tok != NIL; pnode_tok = pnode_tok->next_of_node)
            {
                token* match_tok = pnode_tok->parent;
                wme* match_wme = pnode_tok->w;

                if (!match_tok || !match_timetags_equal(thisAgent, match_tok, match_wme, timetags))
                {
                    continue;
                }

                ms_change temp_msc;
                temp_msc.tok = match_tok;
                temp_msc.w = match_wme;
                temp_msc.p_node = prod->p_node;

                Symbol* match_goal = goal_for_level(thisAgent, match_level);
                if (!match_goal)
                {
                    match_goal = find_goal_for_match_set_change_assertion(thisAgent, &temp_msc);
                }
                if (!match_goal)
                {
                    break;
                }

                ms_change* restored_change = NIL;
                thisAgent->memoryManager->allocate_with_pool(MP_ms_change, &restored_change);
                restored_change->p_node = prod->p_node;
                restored_change->tok = match_tok;
                restored_change->w = match_wme;
                restored_change->inst = NIL;
                restored_change->goal = match_goal;
                restored_change->level = match_goal->id->level;

                insert_at_head_of_dll(thisAgent->ms_i_assertions, restored_change, next, prev);
                insert_at_head_of_dll(match_goal->id->ms_i_assertions, restored_change, next_in_level, prev_in_level);
                insert_at_head_of_dll(prod->p_node->b.p.tentative_assertions, restored_change, next_of_node, prev_of_node);
                prod->OPERAND_which_assert_list = I_LIST;
                ++restored_count;
                break;
            }
        }

        return;
    }
}