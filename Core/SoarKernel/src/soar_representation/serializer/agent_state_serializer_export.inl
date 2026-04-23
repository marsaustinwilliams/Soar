bool symbol_on_goal_chain(agent* thisAgent, Symbol* sym)
{
    if (!thisAgent || !sym || !sym->is_sti())
    {
        return false;
    }

    for (Symbol* goal = thisAgent->top_goal; goal != NIL; goal = goal->id->lower_goal)
    {
        if (goal == sym)
        {
            return true;
        }
    }

    return false;
}

bool serializer_track_identifier_matches(char letter, uint64_t number)
{
    const char* tracked = std::getenv("SOAR_SERIALIZER_TRACK_IDENTIFIER");
    if (!tracked || !tracked[0])
    {
        return false;
    }

    std::string id(1, letter);
    id.append(std::to_string(number));
    return (id == tracked);
}

uint64_t add_symbol_entry(agent* thisAgent,
                          Symbol* sym,
                          soar::kernel::AgentState* state,
                          SymbolIdMap& symbol_map)
{
    auto is_valid_utf8 = [](const char* text)
    {
        if (!text) return true;
        const unsigned char* s = reinterpret_cast<const unsigned char*>(text);
        while (*s)
        {
            if (*s < 0x80)
            {
                ++s;
                continue;
            }

            int continuation = 0;
            if ((*s & 0xE0) == 0xC0)
            {
                continuation = 1;
                if (*s < 0xC2) return false;
            }
            else if ((*s & 0xF0) == 0xE0)
            {
                continuation = 2;
            }
            else if ((*s & 0xF8) == 0xF0)
            {
                continuation = 3;
                if (*s > 0xF4) return false;
            }
            else
            {
                return false;
            }

            ++s;
            for (int i = 0; i < continuation; ++i)
            {
                if ((*s & 0xC0) != 0x80)
                {
                    return false;
                }
                ++s;
            }
        }

        return true;
    };

    auto describe_bytes = [](const char* text)
    {
        std::string out;
        if (!text)
        {
            return std::string("<null>");
        }

        const unsigned char* s = reinterpret_cast<const unsigned char*>(text);
        for (size_t i = 0; (i < 24) && s[i]; ++i)
        {
            char buf[5];
            std::snprintf(buf, sizeof(buf), "%02X", static_cast<unsigned int>(s[i]));
            if (!out.empty()) out.push_back(' ');
            out.append(buf);
        }
        return out;
    };

    if (!sym) return 0;
    auto existing = symbol_map.find(sym);
    if (existing != symbol_map.end()) return existing->second;

    uint64_t new_id = static_cast<uint64_t>(symbol_map.size()) + 1;
    symbol_map[sym] = new_id;

    auto* entry = state->add_symbols();
    entry->set_symbol_id(new_id);
    entry->set_type(symbol_to_proto_type(sym));

    if (sym->is_string())
    {
        if (!is_valid_utf8(sym->sc->name))
        {
            std::fprintf(stderr,
                         "export_invalid_utf8_symbol: sym=%p name_ptr=%p bytes=%s\n",
                         static_cast<void*>(sym),
                         static_cast<const void*>(sym->sc->name),
                         describe_bytes(sym->sc->name).c_str());
        }
        entry->set_string_value(sym->sc->name);
    }
    else if (sym->is_int())
    {
        entry->set_int_value(sym->ic->value);
    }
    else if (sym->is_float())
    {
        entry->set_float_value(sym->fc->value);
    }
    else if (sym->is_sti())
    {
        auto* identifier = entry->mutable_identifier();
        const bool on_goal_chain = symbol_on_goal_chain(thisAgent, sym);
        identifier->set_name_letter(static_cast<uint32_t>(sym->id->name_letter));
        identifier->set_name_number(sym->id->name_number);
        identifier->set_level(static_cast<int32_t>(sym->id->level));
        identifier->set_isa_goal(on_goal_chain && sym->id->isa_goal);
        identifier->set_isa_operator(sym->id->isa_operator != 0);
        identifier->set_isa_impasse(on_goal_chain && sym->id->isa_impasse);
        identifier->set_impasse_type(on_goal_chain ? static_cast<int32_t>(sym->id->impasse_type)
                                                   : static_cast<int32_t>(NONE_IMPASSE_TYPE));

        if (serializer_track_identifier_matches(sym->id->name_letter, sym->id->name_number))
        {
            int in_disconnected_ids = 0;
            for (dl_list* node = thisAgent->disconnected_ids; node != NIL; node = node->next)
            {
                if (node->item == sym)
                {
                    in_disconnected_ids = 1;
                    break;
                }
            }

            int in_ids_with_unknown_level = 0;
            for (dl_list* node = thisAgent->ids_with_unknown_level; node != NIL; node = node->next)
            {
                if (node->item == sym)
                {
                    in_ids_with_unknown_level = 1;
                    break;
                }
            }

            const int in_promoted_ids = symbol_is_in_cons_list(thisAgent->promoted_ids, sym) ? 1 : 0;
            const int in_chunky_problem_spaces = symbol_is_in_cons_list(thisAgent->explanationBasedChunker->chunky_problem_spaces, sym) ? 1 : 0;
            const int in_chunk_free_problem_spaces = symbol_is_in_cons_list(thisAgent->explanationBasedChunker->chunk_free_problem_spaces, sym) ? 1 : 0;

            uint64_t wme_id_hits = 0;
            uint64_t wme_value_hits = 0;
            uint64_t slot_pref_id_hits = 0;
            uint64_t slot_pref_attr_hits = 0;
            uint64_t slot_pref_value_hits = 0;
            uint64_t slot_pref_referent_hits = 0;
            uint64_t slot_osk_pref_value_hits = 0;

            std::unordered_set<slot*> seen_slots;
            for (wme* w = thisAgent->all_wmes_in_rete; w != NIL; w = w->rete_next)
            {
                if (w->id == sym)
                {
                    ++wme_id_hits;
                }
                if (w->value == sym)
                {
                    ++wme_value_hits;
                }

                if (!w->id || !w->attr)
                {
                    continue;
                }

                slot* s = find_slot(w->id, w->attr);
                if (!s || !seen_slots.insert(s).second)
                {
                    continue;
                }

                for (preference* pref = s->all_preferences; pref != NIL; pref = pref->all_of_slot_next)
                {
                    if (pref->id == sym) ++slot_pref_id_hits;
                    if (pref->attr == sym) ++slot_pref_attr_hits;
                    if (pref->value == sym) ++slot_pref_value_hits;
                    if (pref->referent == sym) ++slot_pref_referent_hits;
                }

                for (cons* osk_pref = s->OSK_prefs; osk_pref != NIL; osk_pref = osk_pref->rest)
                {
                    preference* pref = static_cast<preference*>(osk_pref->first);
                    if (pref && (pref->value == sym))
                    {
                        ++slot_osk_pref_value_hits;
                    }
                }
            }

            std::cerr << "[SERIALIZER_TRACK] export_symbol id="
                      << sym->id->name_letter << sym->id->name_number
                      << " symbol_id=" << new_id
                      << " ref=" << sym->reference_count
                      << " level=" << sym->id->level
                      << " link_count=" << sym->id->link_count
                      << " isa_goal=" << (sym->id->isa_goal ? 1 : 0)
                      << " isa_operator=" << (sym->id->isa_operator ? 1 : 0)
                      << " on_goal_chain=" << (on_goal_chain ? 1 : 0)
                      << " in_disconnected_ids=" << in_disconnected_ids
                      << " in_ids_with_unknown_level=" << in_ids_with_unknown_level
                      << " in_promoted_ids=" << in_promoted_ids
                      << " in_chunky_problem_spaces=" << in_chunky_problem_spaces
                      << " in_chunk_free_problem_spaces=" << in_chunk_free_problem_spaces
                      << " wme_id_hits=" << wme_id_hits
                      << " wme_value_hits=" << wme_value_hits
                      << " slot_pref_id_hits=" << slot_pref_id_hits
                      << " slot_pref_attr_hits=" << slot_pref_attr_hits
                      << " slot_pref_value_hits=" << slot_pref_value_hits
                      << " slot_pref_referent_hits=" << slot_pref_referent_hits
                      << " slot_osk_pref_value_hits=" << slot_osk_pref_value_hits
                      << std::endl;
        }
    }
    else if (sym->is_variable())
    {
        if (!is_valid_utf8(sym->var->name))
        {
            std::fprintf(stderr,
                         "export_invalid_utf8_variable_symbol: sym=%p name_ptr=%p bytes=%s\n",
                         static_cast<void*>(sym),
                         static_cast<const void*>(sym->var->name),
                         describe_bytes(sym->var->name).c_str());
        }
        entry->set_variable_name(sym->var->name);
        entry->set_variable_gensym_number(sym->var->gensym_number);
    }

    return new_id;
}

Symbol* condition_test_referent(test condition_test, Symbol* fallback)
{
    if (condition_test && condition_test->eq_test && condition_test->eq_test->data.referent)
    {
        return condition_test->eq_test->data.referent;
    }

    return fallback;
}

void add_wme_entry(agent* thisAgent,
                   wme* w,
                   soar::kernel::AgentState* state,
                   SymbolIdMap& symbol_map,
                   soar::kernel::WmeEntry* entry)
{
    const soar::kernel::WmeOwnerType owner_type = owner_to_proto_type(determine_live_wme_owner(thisAgent, w));

    entry->set_wme_id(reinterpret_cast<uint64_t>(w));
    entry->set_id_symbol(add_symbol_entry(thisAgent, w->id, state, symbol_map));
    entry->set_attr_symbol(add_symbol_entry(thisAgent, w->attr, state, symbol_map));
    entry->set_value_symbol(add_symbol_entry(thisAgent, w->value, state, symbol_map));
    entry->set_acceptable(w->acceptable);
    entry->set_output_link(w->output_link != NIL);
    entry->set_timetag(w->timetag);
    entry->set_owner_type(owner_type);
    if (w->preference && w->preference->inst)
    {
        const uint64_t inst_id = reinterpret_cast<uint64_t>(w->preference->inst);
        entry->set_supporting_instantiation_id(inst_id);
        entry->set_creating_instantiation_id(inst_id);
    }
    if (w->value && w->value->is_sti() && serializer_track_identifier_matches(w->value->id->name_letter, w->value->id->name_number))
    {
        std::cerr << "[SERIALIZER_TRACK] export_wme_value id="
                  << w->value->id->name_letter << w->value->id->name_number
                  << " timetag=" << w->timetag
                  << " output_link=" << (w->output_link ? 1 : 0)
                  << " owner_type=" << static_cast<int>(owner_type)
                  << std::endl;
    }
    if (w->local_singleton_id_identity_set)
    {
        entry->set_local_singleton_id_identity(w->local_singleton_id_identity_set->get_sub_identity());
    }
    if (w->local_singleton_value_identity_set)
    {
        entry->set_local_singleton_value_identity(w->local_singleton_value_identity_set->get_sub_identity());
    }
    if (w->wma_decay_el)
    {
        entry->set_has_wma_decay(true);
        entry->set_wma_just_removed(w->wma_decay_el->just_removed);
        entry->set_wma_just_created(w->wma_decay_el->just_created);
        entry->set_wma_num_references(w->wma_decay_el->num_references);
        entry->set_wma_forget_cycle(w->wma_decay_el->forget_cycle);
        entry->set_wma_history_references(w->wma_decay_el->touches.history_references);
        entry->set_wma_total_references(w->wma_decay_el->touches.total_references);
        entry->set_wma_first_reference(w->wma_decay_el->touches.first_reference);
        entry->set_wma_next_p(static_cast<uint32_t>(w->wma_decay_el->touches.next_p));
        entry->set_wma_history_ct(static_cast<uint32_t>(w->wma_decay_el->touches.history_ct));
        for (int history_index = 0; history_index < WMA_DECAY_HISTORY; ++history_index)
        {
            entry->add_wma_access_cycles(w->wma_decay_el->touches.access_history[history_index].d_cycle);
            entry->add_wma_access_references(w->wma_decay_el->touches.access_history[history_index].num_references);
        }
    }
    if (w->id && w->attr && w->value && w->id->is_sti() &&
        (w->id->to_string(true, false, NIL, 0) == std::string("S2")) &&
        ((w->attr->to_string(true, false, NIL, 0) == std::string("operator")) ||
         (w->attr->to_string(true, false, NIL, 0) == std::string("name"))))
    {
        std::fprintf(stderr,
                     "export_wme_singletons: %s ^%s %s singletons=%llu/%llu\n",
                     w->id->to_string(true, false, NIL, 0),
                     w->attr->to_string(true, false, NIL, 0),
                     w->value->to_string(true, false, NIL, 0),
                     static_cast<unsigned long long>(w->local_singleton_id_identity_set ? w->local_singleton_id_identity_set->get_sub_identity() : 0),
                     static_cast<unsigned long long>(w->local_singleton_value_identity_set ? w->local_singleton_value_identity_set->get_sub_identity() : 0));
    }
    if ((owner_type != soar::kernel::WME_OWNER_IMPASSE) && (w->preference != NIL))
    {
        auto* pref_entry = entry->mutable_preference();
        pref_entry->set_type(static_cast<soar::kernel::PreferenceType>(w->preference->type));
        pref_entry->set_o_supported(w->preference->o_supported);
        pref_entry->set_in_tm(w->preference->in_tm);
        pref_entry->set_id(add_symbol_entry(thisAgent, w->preference->id, state, symbol_map));
        pref_entry->set_attr(add_symbol_entry(thisAgent, w->preference->attr, state, symbol_map));
        pref_entry->set_value(add_symbol_entry(thisAgent, w->preference->value, state, symbol_map));
        pref_entry->set_referent(add_symbol_entry(thisAgent, w->preference->referent, state, symbol_map));
        pref_entry->set_match_goal_level(w->preference->inst ? w->preference->inst->match_goal_level : w->preference->level);
        pref_entry->set_inst_identity_id(w->preference->inst_identities.id);
        pref_entry->set_inst_identity_attr(w->preference->inst_identities.attr);
        pref_entry->set_inst_identity_value(w->preference->inst_identities.value);
        pref_entry->set_inst_identity_referent(w->preference->inst_identities.referent);
        pref_entry->set_identity_id(w->preference->identities.id ? w->preference->identities.id->get_sub_identity() : 0);
        pref_entry->set_identity_attr(w->preference->identities.attr ? w->preference->identities.attr->get_sub_identity() : 0);
        pref_entry->set_identity_value(w->preference->identities.value ? w->preference->identities.value->get_sub_identity() : 0);
        pref_entry->set_identity_referent(w->preference->identities.referent ? w->preference->identities.referent->get_sub_identity() : 0);

        if (w->id && w->attr && w->value && w->id->is_sti() &&
            (w->id->to_string(true, false, NIL, 0) == std::string("S2")) &&
            ((w->attr->to_string(true, false, NIL, 0) == std::string("operator")) ||
             (w->attr->to_string(true, false, NIL, 0) == std::string("name"))))
        {
            std::fprintf(stderr,
                         "export_wme_pref: attr=%s value=%s inst_ids=%llu/%llu/%llu ids=%llu/%llu/%llu\n",
                         w->attr->to_string(true, false, NIL, 0),
                         w->value->to_string(true, false, NIL, 0),
                         static_cast<unsigned long long>(w->preference->inst_identities.id),
                         static_cast<unsigned long long>(w->preference->inst_identities.attr),
                         static_cast<unsigned long long>(w->preference->inst_identities.value),
                         static_cast<unsigned long long>(w->preference->identities.id ? w->preference->identities.id->get_sub_identity() : 0),
                         static_cast<unsigned long long>(w->preference->identities.attr ? w->preference->identities.attr->get_sub_identity() : 0),
                         static_cast<unsigned long long>(w->preference->identities.value ? w->preference->identities.value->get_sub_identity() : 0));
        }
    }
}

void export_all_identifier_symbols(agent* thisAgent,
                                   soar::kernel::AgentState* state,
                                   SymbolIdMap& symbol_map)
{
    for (uint64_t letter_index = 0; letter_index < 26; ++letter_index)
    {
        const char letter = static_cast<char>('A' + letter_index);
        const uint64_t max_number = *thisAgent->symbolManager->get_id_counter(letter_index);
        for (uint64_t number = 1; number < max_number; ++number)
        {
            Symbol* sym = thisAgent->symbolManager->find_identifier(letter, number);
            if (!sym || !sym->is_sti())
            {
                continue;
            }

            add_symbol_entry(thisAgent, sym, state, symbol_map);
        }
    }
}

void add_condition_wme_entry(agent* thisAgent,
                             wme* w,
                             soar::kernel::AgentState* state,
                             SymbolIdMap& symbol_map,
                             soar::kernel::WmeEntry* entry)
{
    if (!w || !entry)
    {
        return;
    }

    entry->set_wme_id(reinterpret_cast<uint64_t>(w));
    entry->set_id_symbol(add_symbol_entry(thisAgent, w->id, state, symbol_map));
    entry->set_attr_symbol(add_symbol_entry(thisAgent, w->attr, state, symbol_map));
    entry->set_value_symbol(add_symbol_entry(thisAgent, w->value, state, symbol_map));
    entry->set_acceptable(w->acceptable);
    entry->set_output_link(w->output_link != NIL);
    entry->set_timetag(w->timetag);
    entry->set_owner_type(w->acceptable ? soar::kernel::WME_OWNER_ACCEPTABLE : soar::kernel::WME_OWNER_SLOT);
    if (w->preference && w->preference->inst)
    {
        const uint64_t inst_id = reinterpret_cast<uint64_t>(w->preference->inst);
        entry->set_supporting_instantiation_id(inst_id);
        entry->set_creating_instantiation_id(inst_id);
    }
    if (w->local_singleton_id_identity_set)
    {
        entry->set_local_singleton_id_identity(w->local_singleton_id_identity_set->get_sub_identity());
    }
    if (w->local_singleton_value_identity_set)
    {
        entry->set_local_singleton_value_identity(w->local_singleton_value_identity_set->get_sub_identity());
    }

    if (w->preference != NIL)
    {
        auto* pref_entry = entry->mutable_preference();
        pref_entry->set_type(static_cast<soar::kernel::PreferenceType>(w->preference->type));
        pref_entry->set_o_supported(w->preference->o_supported);
        pref_entry->set_in_tm(w->preference->in_tm);
        pref_entry->set_id(add_symbol_entry(thisAgent, w->preference->id, state, symbol_map));
        pref_entry->set_attr(add_symbol_entry(thisAgent, w->preference->attr, state, symbol_map));
        pref_entry->set_value(add_symbol_entry(thisAgent, w->preference->value, state, symbol_map));
        pref_entry->set_referent(add_symbol_entry(thisAgent, w->preference->referent, state, symbol_map));
        pref_entry->set_match_goal_level(w->preference->inst ? w->preference->inst->match_goal_level : w->preference->level);
        pref_entry->set_inst_identity_id(w->preference->inst_identities.id);
        pref_entry->set_inst_identity_attr(w->preference->inst_identities.attr);
        pref_entry->set_inst_identity_value(w->preference->inst_identities.value);
        pref_entry->set_inst_identity_referent(w->preference->inst_identities.referent);
        pref_entry->set_identity_id(w->preference->identities.id ? w->preference->identities.id->get_sub_identity() : 0);
        pref_entry->set_identity_attr(w->preference->identities.attr ? w->preference->identities.attr->get_sub_identity() : 0);
        pref_entry->set_identity_value(w->preference->identities.value ? w->preference->identities.value->get_sub_identity() : 0);
        pref_entry->set_identity_referent(w->preference->identities.referent ? w->preference->identities.referent->get_sub_identity() : 0);
    }
}

void add_instantiated_condition_wme_entry(agent* thisAgent,
                                          condition* cond,
                                          soar::kernel::AgentState* state,
                                          SymbolIdMap& symbol_map,
                                          soar::kernel::WmeEntry* entry)
{
    if (!cond || (cond->type != POSITIVE_CONDITION) || !cond->bt.wme_ || !entry)
    {
        return;
    }

    wme* w = cond->bt.wme_;
    Symbol* id = condition_test_referent(cond->data.tests.id_test, w->id);
    Symbol* attr = condition_test_referent(cond->data.tests.attr_test, w->attr);
    Symbol* value = condition_test_referent(cond->data.tests.value_test, w->value);

    if (!id || !attr || !value)
    {
        return;
    }

    entry->set_wme_id(reinterpret_cast<uint64_t>(w));
    entry->set_id_symbol(add_symbol_entry(thisAgent, id, state, symbol_map));
    entry->set_attr_symbol(add_symbol_entry(thisAgent, attr, state, symbol_map));
    entry->set_value_symbol(add_symbol_entry(thisAgent, value, state, symbol_map));
    entry->set_acceptable(w->acceptable);
    entry->set_output_link(w->output_link != NIL);
    entry->set_timetag(w->timetag);
    entry->set_owner_type(w->acceptable ? soar::kernel::WME_OWNER_ACCEPTABLE : soar::kernel::WME_OWNER_SLOT);
    if (w->preference && w->preference->inst)
    {
        entry->set_supporting_instantiation_id(reinterpret_cast<uint64_t>(w->preference->inst));
    }
    if (cond->bt.trace && cond->bt.trace->inst)
    {
        entry->set_creating_instantiation_id(reinterpret_cast<uint64_t>(cond->bt.trace->inst));
    }
    else if (w->preference && w->preference->inst)
    {
        entry->set_creating_instantiation_id(reinterpret_cast<uint64_t>(w->preference->inst));
    }
    if (w->local_singleton_id_identity_set)
    {
        entry->set_local_singleton_id_identity(w->local_singleton_id_identity_set->get_sub_identity());
    }
    if (w->local_singleton_value_identity_set)
    {
        entry->set_local_singleton_value_identity(w->local_singleton_value_identity_set->get_sub_identity());
    }
}

void add_preference_entry(agent* thisAgent,
                          preference* pref,
                          soar::kernel::AgentState* state,
                          SymbolIdMap& symbol_map,
                          soar::kernel::PreferenceEntry* pref_entry)
{
    if (!pref || !pref_entry)
    {
        return;
    }

    if (!preference_type_is_valid(static_cast<int>(pref->type)))
    {
        return;
    }

    pref_entry->set_type(static_cast<soar::kernel::PreferenceType>(pref->type));
    pref_entry->set_o_supported(pref->o_supported);
    pref_entry->set_in_tm(pref->in_tm);
    pref_entry->set_id(add_symbol_entry(thisAgent, pref->id, state, symbol_map));
    pref_entry->set_attr(add_symbol_entry(thisAgent, pref->attr, state, symbol_map));
    pref_entry->set_value(add_symbol_entry(thisAgent, pref->value, state, symbol_map));
    pref_entry->set_referent(add_symbol_entry(thisAgent, pref->referent, state, symbol_map));
    pref_entry->set_match_goal_level(pref->inst ? pref->inst->match_goal_level : pref->level);
    pref_entry->set_inst_identity_id(pref->inst_identities.id);
    pref_entry->set_inst_identity_attr(pref->inst_identities.attr);
    pref_entry->set_inst_identity_value(pref->inst_identities.value);
    pref_entry->set_inst_identity_referent(pref->inst_identities.referent);
    pref_entry->set_identity_id(pref->identities.id ? pref->identities.id->get_sub_identity() : 0);
    pref_entry->set_identity_attr(pref->identities.attr ? pref->identities.attr->get_sub_identity() : 0);
    pref_entry->set_identity_value(pref->identities.value ? pref->identities.value->get_sub_identity() : 0);
    pref_entry->set_identity_referent(pref->identities.referent ? pref->identities.referent->get_sub_identity() : 0);

    if (pref->value && pref->value->is_sti() &&
        serializer_track_identifier_matches(pref->value->id->name_letter, pref->value->id->name_number))
    {
        std::cerr << "[SERIALIZER_TRACK] export_pref_value id="
                  << pref->value->id->name_letter << pref->value->id->name_number
                  << " type=" << static_cast<int>(pref->type)
                  << " in_tm=" << (pref->in_tm ? 1 : 0)
                  << " o_supported=" << (pref->o_supported ? 1 : 0)
                  << " level=" << static_cast<int>(pref->inst ? pref->inst->match_goal_level : pref->level)
                  << " inst="
                  << ((pref->inst && pref->inst->prod_name && pref->inst->prod_name->is_string())
                          ? pref->inst->prod_name->sc->name
                          : "<nil>")
                  << std::endl;
    }
}

void add_saved_instantiation_entry(agent* thisAgent,
                                   instantiation* inst,
                                   soar::kernel::AgentState* state,
                                   SymbolIdMap& symbol_map,
                                   SavedInstantiationSet& exported_instantiations)
{
    const bool validate_saved_instantiation_stages =
        current_save_cycle_staged_validation_enabled() ||
        (std::getenv("SOAR_VALIDATE_SAVED_INSTANTIATION_STAGES") != nullptr);

    auto validate_state_stage = [&](const char* stage)
    {
        if (!validate_saved_instantiation_stages)
        {
            return true;
        }

        std::string bytes;
        if (!state->SerializePartialToString(&bytes))
        {
            std::fprintf(stderr,
                         "add_saved_instantiation_entry serialize failed: stage=%s prod=%s in_ms=%d\n",
                         stage,
                         (inst && inst->prod_name && inst->prod_name->is_string()) ? inst->prod_name->sc->name : "<nil>",
                         (inst && inst->in_ms) ? 1 : 0);
            return false;
        }

        soar::kernel::AgentState parsed;
        if (!parsed.ParsePartialFromString(bytes))
        {
            std::fprintf(stderr,
                         "add_saved_instantiation_entry parse failed: stage=%s prod=%s in_ms=%d bytes=%llu entries=%llu symbols=%llu\n",
                         stage,
                         (inst && inst->prod_name && inst->prod_name->is_string()) ? inst->prod_name->sc->name : "<nil>",
                         (inst && inst->in_ms) ? 1 : 0,
                         static_cast<unsigned long long>(bytes.size()),
                         static_cast<unsigned long long>(state->live_match_instantiations_size()),
                         static_cast<unsigned long long>(state->symbols_size()));
            return false;
        }

        return true;
    };

    std::vector<uint64_t> instantiated_condition_timetags;
    const bool has_serialized_conditions = capture_instantiated_condition_timetags(inst, instantiated_condition_timetags);

    const bool detached_condition_instantiation = instantiation_has_only_detached_condition_wmes(thisAgent, inst);
    const bool serialize_full_instantiated_conditions =
        instantiation_has_any_detached_condition_wmes(thisAgent, inst);

    if (!inst || detached_condition_instantiation || !inst->prod || !inst->prod_name || !inst->prod_name->is_string() || (!inst->rete_token && !has_serialized_conditions))
    {
        if (inst && inst->prod_name && inst->prod_name->is_string())
        {
            const char* prod_name = inst->prod_name->sc->name;
            if (!std::strcmp(prod_name, "apply-op1") || !std::strcmp(prod_name, "prefer*scottie"))
            {
                std::fprintf(stderr,
                             "skip_export_instantiation: %s rete_token=%d in_ms=%d synthetic=%d detached=%d\n",
                             prod_name,
                             inst->rete_token ? 1 : 0,
                             inst->in_ms ? 1 : 0,
                             0,
                             detached_condition_instantiation ? 1 : 0);
            }
        }
        return;
    }

    if (!exported_instantiations.insert(inst).second)
    {
        return;
    }

    if (inst->prod_name && inst->prod_name->is_string())
    {
        const char* prod_name = inst->prod_name->sc->name;
        if (!std::strcmp(prod_name, "prefer*scottie") ||
            !std::strcmp(prod_name, "propose1") ||
            !std::strcmp(prod_name, "apply-op1") ||
            !std::strcmp(prod_name, "propose*stage*2") ||
            !std::strcmp(prod_name, "make-chunk"))
        {
            if (!std::strcmp(prod_name, "apply-op1"))
            {
                for (condition* debug_cond = inst->top_of_instantiated_conditions; debug_cond != NIL; debug_cond = debug_cond->next)
                {
                    if (debug_cond->type != POSITIVE_CONDITION)
                    {
                        continue;
                    }

                    std::string id_test;
                    std::string attr_test;
                    std::string value_test;
                    thisAgent->outputManager->sprinta_sf(thisAgent, id_test, "%t", debug_cond->data.tests.id_test);
                    thisAgent->outputManager->sprinta_sf(thisAgent, attr_test, "%t", debug_cond->data.tests.attr_test);
                    thisAgent->outputManager->sprinta_sf(thisAgent, value_test, "%t", debug_cond->data.tests.value_test);

                    test id_eq = debug_cond->data.tests.id_test->eq_test;
                    test attr_eq = debug_cond->data.tests.attr_test->eq_test;
                    test value_eq = debug_cond->data.tests.value_test->eq_test;

                    std::fprintf(stderr,
                                 "export_apply_op1_cond: %s | %s | %s | ids=%llu/%llu/%llu\n",
                                 id_test.c_str(),
                                 attr_test.c_str(),
                                 value_test.c_str(),
                                 static_cast<unsigned long long>(id_eq && id_eq->identity ? id_eq->identity->get_sub_identity() : 0),
                                 static_cast<unsigned long long>(attr_eq && attr_eq->identity ? attr_eq->identity->get_sub_identity() : 0),
                                 static_cast<unsigned long long>(value_eq && value_eq->identity ? value_eq->identity->get_sub_identity() : 0));
                }
            }

            size_t generated_pref_count = 0;
            for (preference* pref = inst->preferences_generated; pref != NIL; pref = pref->inst_next)
            {
                ++generated_pref_count;
            }

            size_t osk_pref_count = 0;
            for (cons* osk_pref = inst->OSK_prefs; osk_pref != NIL; osk_pref = osk_pref->rest)
            {
                ++osk_pref_count;
            }

            size_t osk_proposal_pref_count = 0;
            for (cons* osk_pref = inst->OSK_proposal_prefs; osk_pref != NIL; osk_pref = osk_pref->rest)
            {
                ++osk_proposal_pref_count;
            }

            std::fprintf(stderr,
                         "export_instantiation: %s in_ms=%d prefs=%llu osk=%llu osk_prop=%llu conds=%llu\n",
                         prod_name,
                         inst->in_ms ? 1 : 0,
                         static_cast<unsigned long long>(generated_pref_count),
                         static_cast<unsigned long long>(osk_pref_count),
                         static_cast<unsigned long long>(osk_proposal_pref_count),
                         static_cast<unsigned long long>(instantiated_condition_timetags.size()));
        }
    }

    auto* entry = state->add_live_match_instantiations();
    entry->set_production_name(inst->prod_name->sc->name);
    entry->set_match_goal_level(static_cast<int32_t>(inst->match_goal_level));
    entry->set_in_ms(inst->in_ms);
    entry->set_instantiation_id(reinterpret_cast<uint64_t>(inst));
    if (!validate_state_stage("base")) return;

    if (inst->rete_token)
    {
        std::vector<uint64_t> timetags = capture_match_timetags(thisAgent, inst->rete_token, inst->rete_wme);
        for (uint64_t timetag : timetags)
        {
            entry->add_timetags(timetag);
        }
    }

    for (uint64_t timetag : instantiated_condition_timetags)
    {
        entry->add_instantiated_condition_timetags(timetag);
    }
    if (!validate_state_stage("timetags")) return;

    if (serialize_full_instantiated_conditions)
    {
        size_t cond_index = 0;
        for (condition* cond = inst->top_of_instantiated_conditions; cond != NIL; cond = cond->next)
        {
            if ((cond->type == POSITIVE_CONDITION) && cond->bt.wme_)
            {
                add_instantiated_condition_wme_entry(thisAgent, cond, state, symbol_map, entry->add_instantiated_condition_wmes());
                if (!validate_state_stage("inst_cond_wme_list"))
                {
                    std::fprintf(stderr,
                                 "inst_cond_failure_detail: prod=%s idx=%llu step=wme_list timetag=%llu\n",
                                 inst->prod_name->sc->name,
                                 static_cast<unsigned long long>(cond_index),
                                 static_cast<unsigned long long>(cond->bt.wme_->timetag));
                    return;
                }

                auto* condition_entry = entry->add_instantiated_conditions();
                add_instantiated_condition_wme_entry(thisAgent, cond, state, symbol_map, condition_entry->mutable_wme());
                if (!validate_state_stage("inst_cond_entry_wme"))
                {
                    std::fprintf(stderr,
                                 "inst_cond_failure_detail: prod=%s idx=%llu step=entry_wme timetag=%llu\n",
                                 inst->prod_name->sc->name,
                                 static_cast<unsigned long long>(cond_index),
                                 static_cast<unsigned long long>(cond->bt.wme_->timetag));
                    return;
                }
                condition_entry->set_id_inst_identity(cond->data.tests.id_test && cond->data.tests.id_test->eq_test ? cond->data.tests.id_test->eq_test->inst_identity : 0);
                condition_entry->set_attr_inst_identity(cond->data.tests.attr_test && cond->data.tests.attr_test->eq_test ? cond->data.tests.attr_test->eq_test->inst_identity : 0);
                condition_entry->set_value_inst_identity(cond->data.tests.value_test && cond->data.tests.value_test->eq_test ? cond->data.tests.value_test->eq_test->inst_identity : 0);
                condition_entry->set_id_identity(cond->data.tests.id_test && cond->data.tests.id_test->eq_test && cond->data.tests.id_test->eq_test->identity ? cond->data.tests.id_test->eq_test->identity->get_sub_identity() : 0);
                condition_entry->set_attr_identity(cond->data.tests.attr_test && cond->data.tests.attr_test->eq_test && cond->data.tests.attr_test->eq_test->identity ? cond->data.tests.attr_test->eq_test->identity->get_sub_identity() : 0);
                condition_entry->set_value_identity(cond->data.tests.value_test && cond->data.tests.value_test->eq_test && cond->data.tests.value_test->eq_test->identity ? cond->data.tests.value_test->eq_test->identity->get_sub_identity() : 0);
                if (!validate_state_stage("inst_cond_identities"))
                {
                    std::fprintf(stderr,
                                 "inst_cond_failure_detail: prod=%s idx=%llu step=identities timetag=%llu\n",
                                 inst->prod_name->sc->name,
                                 static_cast<unsigned long long>(cond_index),
                                 static_cast<unsigned long long>(cond->bt.wme_->timetag));
                    return;
                }
                if (cond->bt.trace)
                {
                    add_preference_entry(thisAgent,
                                         cond->bt.trace,
                                         state,
                                         symbol_map,
                                         condition_entry->mutable_trace_preference());
                    if (!validate_state_stage("inst_cond_trace_preference"))
                    {
                        std::fprintf(stderr,
                                     "inst_cond_failure_detail: prod=%s idx=%llu step=trace_pref timetag=%llu\n",
                                     inst->prod_name->sc->name,
                                     static_cast<unsigned long long>(cond_index),
                                     static_cast<unsigned long long>(cond->bt.wme_->timetag));
                        return;
                    }
                }

                if (inst->prod_name && inst->prod_name->is_string() && !std::strcmp(inst->prod_name->sc->name, "apply-op1"))
                {
                    std::string id_test;
                    std::string attr_test;
                    std::string value_test;
                    thisAgent->outputManager->sprinta_sf(thisAgent, id_test, "%t", cond->data.tests.id_test);
                    thisAgent->outputManager->sprinta_sf(thisAgent, attr_test, "%t", cond->data.tests.attr_test);
                    thisAgent->outputManager->sprinta_sf(thisAgent, value_test, "%t", cond->data.tests.value_test);
                    std::fprintf(stderr,
                                 "export_apply_op1_trace: %s | %s | %s | trace_inst=%s trace_slot=%s ^%s\n",
                                 id_test.c_str(),
                                 attr_test.c_str(),
                                 value_test.c_str(),
                                 (cond->bt.trace && cond->bt.trace->inst && cond->bt.trace->inst->prod_name && cond->bt.trace->inst->prod_name->is_string()) ? cond->bt.trace->inst->prod_name->sc->name : "<arch>",
                                 (cond->bt.trace && cond->bt.trace->slot && cond->bt.trace->slot->id) ? cond->bt.trace->slot->id->to_string(true, false, NIL, 0) : "<nil>",
                                 (cond->bt.trace && cond->bt.trace->slot && cond->bt.trace->slot->attr) ? cond->bt.trace->slot->attr->to_string(true, false, NIL, 0) : "<nil>");
                }
            }
            ++cond_index;
        }
    }
    if (!validate_state_stage("instantiated_conditions")) return;

    for (preference* pref = inst->preferences_generated; pref != NIL; pref = pref->inst_next)
    {
        add_preference_entry(thisAgent, pref, state, symbol_map, entry->add_preferences_generated());
    }
    if (!validate_state_stage("preferences_generated")) return;

    for (cons* osk_pref = inst->OSK_prefs; osk_pref != NIL; osk_pref = osk_pref->rest)
    {
        preference* pref = static_cast<preference*>(osk_pref->first);
        if (!pref)
        {
            continue;
        }

        add_preference_entry(thisAgent, pref, state, symbol_map, entry->add_osk_prefs());
    }
    if (!validate_state_stage("osk_prefs")) return;

    for (cons* osk_pref = inst->OSK_proposal_prefs; osk_pref != NIL; osk_pref = osk_pref->rest)
    {
        preference* pref = static_cast<preference*>(osk_pref->first);
        if (!pref)
        {
            continue;
        }

        add_preference_entry(thisAgent, pref, state, symbol_map, entry->add_osk_proposal_prefs());
    }
    if (!validate_state_stage("osk_proposal_prefs")) return;

    if (inst->OSK_proposal_slot)
    {
        entry->set_osk_proposal_slot_id(add_symbol_entry(thisAgent, inst->OSK_proposal_slot->id, state, symbol_map));
        entry->set_osk_proposal_slot_attr(add_symbol_entry(thisAgent, inst->OSK_proposal_slot->attr, state, symbol_map));
    }
    if (!validate_state_stage("osk_proposal_slot")) return;

    if (entry->osk_proposal_prefs_size() > 0)
    {
        std::fprintf(stderr,
                 "export_live_match_instantiations: %s osk_proposal_prefs=%llu in_ms=%d\n",
                 inst->prod_name->sc->name,
                 static_cast<unsigned long long>(entry->osk_proposal_prefs_size()),
                 inst->in_ms ? 1 : 0);
    }
}

void fill_state_settings(agent* thisAgent,
                         soar::kernel::AgentState* state,
                         SymbolIdMap& symbol_map)
{
    auto* settings = state->mutable_settings();
    settings->set_current_phase(static_cast<int32_t>(thisAgent->current_phase));
    settings->set_stop_soar(thisAgent->stop_soar);
    settings->set_system_halted(thisAgent->system_halted);
    if (thisAgent->reason_for_stopping)
    {
        settings->set_reason_for_stopping(thisAgent->reason_for_stopping);
    }
    settings->set_go_number(static_cast<uint64_t>(thisAgent->go_number));
    settings->set_go_slot_attr_symbol(add_symbol_entry(thisAgent, thisAgent->go_slot_attr, state, symbol_map));
    settings->set_go_slot_level(static_cast<int32_t>(thisAgent->go_slot_level));
    settings->set_go_type(static_cast<int32_t>(thisAgent->go_type));
    settings->set_input_period(thisAgent->input_period);
    settings->set_input_cycle_flag(thisAgent->input_cycle_flag);
    settings->set_current_wme_timetag(thisAgent->current_wme_timetag);
    settings->set_bottom_goal_symbol(add_symbol_entry(thisAgent, thisAgent->bottom_goal, state, symbol_map));
    settings->set_top_goal_symbol(add_symbol_entry(thisAgent, thisAgent->top_goal, state, symbol_map));
    settings->set_top_state_symbol(add_symbol_entry(thisAgent, thisAgent->top_state, state, symbol_map));
    if (thisAgent->name_of_production_being_reordered)
    {
        settings->set_name_of_production_being_reordered(thisAgent->name_of_production_being_reordered);
    }

    for (int i = 0; i <= HIGHEST_SYSPARAM_NUMBER; ++i)
    {
        settings->add_trace_settings(thisAgent->trace_settings[i]);
    }
}

void fill_state_counters(agent* thisAgent, soar::kernel::AgentState* state)
{
    auto* counters = state->mutable_counters();
    counters->set_cumulative_wm_size(thisAgent->cumulative_wm_size);
    counters->set_num_wm_sizes_accumulated(thisAgent->num_wm_sizes_accumulated);
    counters->set_max_wm_size(thisAgent->max_wm_size);
    counters->set_wme_addition_count(thisAgent->wme_addition_count);
    counters->set_wme_removal_count(thisAgent->wme_removal_count);
    counters->set_init_count(thisAgent->init_count);
    counters->set_d_cycle_count(thisAgent->d_cycle_count);
    counters->set_e_cycle_count(thisAgent->e_cycle_count);
    counters->set_e_cycles_this_d_cycle(thisAgent->e_cycles_this_d_cycle);
    counters->set_num_existing_wmes(thisAgent->num_existing_wmes);
    counters->set_production_firing_count(thisAgent->production_firing_count);
    counters->set_start_dc_production_firing_count(thisAgent->start_dc_production_firing_count);
    counters->set_start_dc_wme_addition_count(thisAgent->start_dc_wme_addition_count);
    counters->set_start_dc_wme_removal_count(thisAgent->start_dc_wme_removal_count);
    counters->set_max_dc_production_firing_count_value(thisAgent->max_dc_production_firing_count_value);
    counters->set_max_dc_production_firing_count_cycle(thisAgent->max_dc_production_firing_count_cycle);
    counters->set_max_dc_wm_changes_value(thisAgent->max_dc_wm_changes_value);
    counters->set_max_dc_wm_changes_cycle(thisAgent->max_dc_wm_changes_cycle);
    counters->set_d_cycle_last_output(thisAgent->d_cycle_last_output);
    counters->set_decide_phases_count(thisAgent->decide_phases_count);
    counters->set_run_phase_count(thisAgent->run_phase_count);
    counters->set_run_elaboration_count(thisAgent->run_elaboration_count);
    counters->set_run_last_output_count(thisAgent->run_last_output_count);
    counters->set_run_generated_output_count(thisAgent->run_generated_output_count);
    counters->set_pe_cycle_count(thisAgent->pe_cycle_count);
    counters->set_pe_cycles_this_d_cycle(thisAgent->pe_cycles_this_d_cycle);
    counters->set_inner_e_cycle_count(thisAgent->inner_e_cycle_count);

    counters->set_current_retesave_amindex(thisAgent->current_retesave_amindex);
    counters->set_reteload_num_ams(thisAgent->reteload_num_ams);
    counters->set_current_retesave_symindex(thisAgent->current_retesave_symindex);
    counters->set_reteload_num_syms(thisAgent->reteload_num_syms);
    counters->set_alpha_mem_id_counter(thisAgent->alpha_mem_id_counter);
    counters->set_beta_node_id_counter(thisAgent->beta_node_id_counter);
    counters->set_current_tc_number(thisAgent->current_tc_number);

    uint64_t t1 = 0;
    uint64_t elapsed = 0;
    double raw_per_usec = 0.0;
    bool running = false;

    thisAgent->timers_cpu.export_state(t1, elapsed, raw_per_usec, running);
    auto* timers_cpu = counters->mutable_timers_cpu();
    timers_cpu->set_t1(t1);
    timers_cpu->set_elapsed(elapsed);
    timers_cpu->set_raw_per_usec(raw_per_usec);
    timers_cpu->set_running(running);

    thisAgent->timers_kernel.export_state(t1, elapsed, raw_per_usec, running);
    auto* timers_kernel = counters->mutable_timers_kernel();
    timers_kernel->set_t1(t1);
    timers_kernel->set_elapsed(elapsed);
    timers_kernel->set_raw_per_usec(raw_per_usec);
    timers_kernel->set_running(running);

    thisAgent->timers_phase.export_state(t1, elapsed, raw_per_usec, running);
    auto* timers_phase = counters->mutable_timers_phase();
    timers_phase->set_t1(t1);
    timers_phase->set_elapsed(elapsed);
    timers_phase->set_raw_per_usec(raw_per_usec);
    timers_phase->set_running(running);

    counters->set_timers_snapshot_raw_time(get_raw_time());

    counters->mutable_timers_total_cpu_time()->set_total(thisAgent->timers_total_cpu_time.get_usec());
    counters->mutable_timers_total_kernel_time()->set_total(thisAgent->timers_total_kernel_time.get_usec());

    for (int i = 0; i < NUM_PHASE_TYPES; ++i)
    {
        counters->add_timers_decision_cycle_phase_usec(thisAgent->timers_decision_cycle_phase[i].get_usec());
    }
    counters->set_timers_input_function_cpu_time_usec(thisAgent->timers_input_function_cpu_time.get_usec());
    counters->set_timers_output_function_cpu_time_usec(thisAgent->timers_output_function_cpu_time.get_usec());

    for (int i = 0; i < NUMBER_OF_CALLBACKS; ++i)
    {
        counters->add_callback_timers_usec(thisAgent->callback_timers[i].get_usec());
    }

    for (int i = 0; i < NUM_PHASE_TYPES; ++i)
    {
        counters->add_timers_monitors_cpu_time_usec(thisAgent->timers_monitors_cpu_time[i].get_usec());
    }

    counters->set_last_derived_kernel_time_usec(thisAgent->last_derived_kernel_time_usec);
    counters->set_max_dc_time_usec(thisAgent->max_dc_time_usec);
    counters->set_max_dc_time_cycle(thisAgent->max_dc_time_cycle);
    counters->set_max_dc_epmem_time_sec(thisAgent->max_dc_epmem_time_sec);
    counters->set_total_dc_epmem_time_sec(thisAgent->total_dc_epmem_time_sec);
    counters->set_max_dc_epmem_time_cycle(thisAgent->max_dc_epmem_time_cycle);
    counters->set_max_dc_smem_time_sec(thisAgent->max_dc_smem_time_sec);
    counters->set_total_dc_smem_time_sec(thisAgent->total_dc_smem_time_sec);
    counters->set_max_dc_smem_time_cycle(thisAgent->max_dc_smem_time_cycle);

    counters->set_tf_printing_tc(static_cast<uint64_t>(thisAgent->tf_printing_tc));
    counters->set_output_link_tc_num(static_cast<uint64_t>(thisAgent->output_link_tc_num));

    counters->set_serializer_runtime_parity_v1(true);
}

void gather_wmes(agent* thisAgent,
                 soar::kernel::AgentState* state,
                 SymbolIdMap& symbol_map)
{
    const bool serialize_buffered_deltas = !(thisAgent->system_halted || thisAgent->stop_soar);
    std::unordered_set<wme*> active_wmes;
    for (wme* w = thisAgent->all_wmes_in_rete; w != NIL; w = w->rete_next)
    {
        active_wmes.insert(w);
        add_wme_entry(thisAgent, w, state, symbol_map, state->add_active_wmes());
    }

    if (!serialize_buffered_deltas)
    {
        return;
    }

    for (cons* c = thisAgent->wmes_to_add; c != NIL; c = c->rest)
    {
        wme* w = static_cast<wme*>(c->first);
        if ((active_wmes.find(w) == active_wmes.end()) && !equivalent_wme_exists_in_rete(thisAgent, w))
        {
            add_wme_entry(thisAgent, w, state, symbol_map, state->add_buffered_add_wmes());
        }
    }

    for (cons* c = thisAgent->wmes_to_remove; c != NIL; c = c->rest)
    {
        wme* w = static_cast<wme*>(c->first);
        add_wme_entry(thisAgent, w, state, symbol_map, state->add_buffered_remove_wmes());
    }
}

bool export_semantic_memory(agent* thisAgent, soar::kernel::AgentState* state)
{
    if (!thisAgent->SMem->enabled() || !thisAgent->SMem->connected())
    {
        state->set_smem_enabled(false);
        return true;
    }

    state->set_smem_enabled(true);
    std::string export_text;
    std::string* err = new std::string();
    bool result = thisAgent->SMem->export_smem(0, export_text, &err);
    if (!result)
    {
        thisAgent->outputManager->printa_sf(thisAgent, "Error exporting semantic memory: %s\n", err->c_str());
        delete err;
        return false;
    }
    delete err;
    state->set_smem_export_text(export_text);
    return true;
}

void export_production_firing_counts(agent* thisAgent, soar::kernel::AgentState* state)
{
    for (int production_type = 0; production_type < NUM_PRODUCTION_TYPES; ++production_type)
    {
        for (production* prod = thisAgent->all_productions_of_type[production_type]; prod != NIL; prod = prod->next)
        {
            if (!prod->name || !prod->name->is_string())
            {
                continue;
            }

            auto* entry = state->add_production_firing_counts();
            entry->set_production_name(prod->name->sc->name);
            entry->set_firing_count(prod->firing_count);
        }
    }
}

void export_rng_state(soar::kernel::AgentState* state)
{
    if (!state)
    {
        return;
    }

    const uint32_t word_count = SoarRngStateWordCount();
    if (word_count == 0)
    {
        state->clear_rng_state();
        return;
    }

    std::vector<uint32_t> rng_state(word_count, 0);
    SoarSaveRNGState(rng_state.data(), word_count);

    state->clear_rng_state();
    for (uint32_t word : rng_state)
    {
        state->add_rng_state(word);
    }
}

void export_live_match_instantiations(agent* thisAgent,
                                      soar::kernel::AgentState* state,
                                      SymbolIdMap& symbol_map)
{
    auto validate_export_state = [&](instantiation* inst, const char* source_tag)
    {
        if (!inst || !inst->prod_name || !inst->prod_name->is_string())
        {
            return true;
        }

        std::string bytes;
        if (!state->SerializePartialToString(&bytes))
        {
            std::fprintf(stderr,
                         "export_live_match_instantiations serialize failed: source=%s prod=%s in_ms=%d\n",
                         source_tag,
                         inst->prod_name->sc->name,
                         inst->in_ms ? 1 : 0);
            return false;
        }

        soar::kernel::AgentState parsed;
        if (!parsed.ParsePartialFromString(bytes))
        {
            std::fprintf(stderr,
                         "export_live_match_instantiations parse failed: source=%s prod=%s in_ms=%d bytes=%llu entries=%llu symbols=%llu\n",
                         source_tag,
                         inst->prod_name->sc->name,
                         inst->in_ms ? 1 : 0,
                         static_cast<unsigned long long>(bytes.size()),
                         static_cast<unsigned long long>(state->live_match_instantiations_size()),
                         static_cast<unsigned long long>(state->symbols_size()));
            return false;
        }

        return true;
    };

    SavedInstantiationSet exported_instantiations;
    size_t exported_live_instantiations = 0;
    size_t exported_nonlive_instantiations = 0;

    for (int production_type = 0; production_type < NUM_PRODUCTION_TYPES; ++production_type)
    {
        for (production* prod = thisAgent->all_productions_of_type[production_type]; prod != NIL; prod = prod->next)
        {
            if (!prod->name || !prod->name->is_string())
            {
                continue;
            }

            for (instantiation* inst = prod->instantiations; inst != NIL; inst = inst->next)
            {
                if (!inst->in_ms)
                {
                    continue;
                }

                const size_t before = exported_instantiations.size();
                add_saved_instantiation_entry(thisAgent, inst, state, symbol_map, exported_instantiations);
                if (exported_instantiations.size() != before && !validate_export_state(inst, "in-ms"))
                {
                    return;
                }
                ++exported_live_instantiations;
            }
        }
    }

    for (Symbol* goal = thisAgent->top_goal; goal != NIL; goal = goal->id->lower_goal)
    {
        if (!goal->is_sti() || !goal->id->operator_slot || !goal->id->operator_slot->instantiation_with_temp_OSK)
        {
            continue;
        }

        const size_t before = exported_instantiations.size();
        add_saved_instantiation_entry(thisAgent,
                                      goal->id->operator_slot->instantiation_with_temp_OSK,
                                      state,
                                      symbol_map,
                                      exported_instantiations);
        if (exported_instantiations.size() != before &&
            !validate_export_state(goal->id->operator_slot->instantiation_with_temp_OSK, "temp-osk"))
        {
            return;
        }
        if (exported_instantiations.size() != before && !goal->id->operator_slot->instantiation_with_temp_OSK->in_ms)
        {
            ++exported_nonlive_instantiations;
        }
    }

    std::unordered_set<slot*> exported_osk_slots;
    for (wme* w = thisAgent->all_wmes_in_rete; w != NIL; w = w->rete_next)
    {
        slot* s = (w->id && w->attr) ? find_slot(w->id, w->attr) : NIL;
        if (!s || !s->OSK_prefs || !exported_osk_slots.insert(s).second)
        {
            continue;
        }

        for (cons* osk_pref = s->OSK_prefs; osk_pref != NIL; osk_pref = osk_pref->rest)
        {
            preference* pref = static_cast<preference*>(osk_pref->first);
            if (!pref || !pref->inst)
            {
                continue;
            }

            const size_t before = exported_instantiations.size();
            add_saved_instantiation_entry(thisAgent, pref->inst, state, symbol_map, exported_instantiations);
            if (exported_instantiations.size() != before && !validate_export_state(pref->inst, "slot-osk-pref"))
            {
                return;
            }
            if (exported_instantiations.size() != before && !pref->inst->in_ms)
            {
                ++exported_nonlive_instantiations;
            }
        }
    }

    for (wme* w = thisAgent->all_wmes_in_rete; w != NIL; w = w->rete_next)
    {
        if (w->preference)
        {
            if (!w->preference->inst)
            {
                continue;
            }
            const size_t before = exported_instantiations.size();
            add_saved_instantiation_entry(thisAgent, w->preference->inst, state, symbol_map, exported_instantiations);
            if (exported_instantiations.size() != before && !validate_export_state(w->preference->inst, "rete-wme-pref"))
            {
                return;
            }
            if (exported_instantiations.size() != before && w->preference->inst && !w->preference->inst->in_ms)
            {
                ++exported_nonlive_instantiations;
            }
        }
    }

    for (cons* c = thisAgent->wmes_to_add; c != NIL; c = c->rest)
    {
        wme* w = static_cast<wme*>(c->first);
        if (w && w->preference)
        {
            if (!w->preference->inst)
            {
                continue;
            }
            const size_t before = exported_instantiations.size();
            add_saved_instantiation_entry(thisAgent, w->preference->inst, state, symbol_map, exported_instantiations);
            if (exported_instantiations.size() != before && !validate_export_state(w->preference->inst, "buffered-add-wme-pref"))
            {
                return;
            }
            if (exported_instantiations.size() != before && w->preference->inst && !w->preference->inst->in_ms)
            {
                ++exported_nonlive_instantiations;
            }
        }
    }

}

rhs_value* rhs_field_ptr(action* act, soar::kernel::ProductionRhsField field)
{
    if (!act || act->type != MAKE_ACTION)
    {
        return nullptr;
    }

    switch (field)
    {
        case soar::kernel::PRODUCTION_RHS_FIELD_ID:
            return &act->id;
        case soar::kernel::PRODUCTION_RHS_FIELD_ATTR:
            return &act->attr;
        case soar::kernel::PRODUCTION_RHS_FIELD_VALUE:
            return &act->value;
        case soar::kernel::PRODUCTION_RHS_FIELD_REFERENT:
            return &act->referent;
        case soar::kernel::PRODUCTION_RHS_FIELD_UNKNOWN:
        default:
            return nullptr;
    }
}

void maybe_export_identifier_rhs_patch(agent* thisAgent,
                                       soar::kernel::AgentState* state,
                                       SymbolIdMap& symbol_map,
                                       const std::string& production_name,
                                       uint64_t action_index,
                                       soar::kernel::ProductionRhsField field,
                                       rhs_value rv)
{
    if (!rv || !rhs_value_is_symbol(rv))
    {
        return;
    }

    Symbol* sym = rhs_value_to_symbol(rv);
    if (!sym || !sym->is_sti())
    {
        return;
    }

    auto* patch = state->add_production_rhs_identifier_patches();
    patch->set_production_name(production_name);
    patch->set_action_index(action_index);
    patch->set_field(field);
    patch->set_symbol_id(add_symbol_entry(thisAgent, sym, state, symbol_map));
}

void export_production_rhs_identifier_patches(agent* thisAgent,
                                              soar::kernel::AgentState* state,
                                              SymbolIdMap& symbol_map)
{
    for (int production_type = 0; production_type < NUM_PRODUCTION_TYPES; ++production_type)
    {
        for (production* prod = thisAgent->all_productions_of_type[production_type]; prod != NIL; prod = prod->next)
        {
            if (!prod->name || !prod->name->is_string())
            {
                continue;
            }

            uint64_t action_index = 0;
            for (action* act = prod->action_list; act != NIL; act = act->next, ++action_index)
            {
                maybe_export_identifier_rhs_patch(thisAgent, state, symbol_map, prod->name->sc->name, action_index, soar::kernel::PRODUCTION_RHS_FIELD_ID, act->id);
                maybe_export_identifier_rhs_patch(thisAgent, state, symbol_map, prod->name->sc->name, action_index, soar::kernel::PRODUCTION_RHS_FIELD_ATTR, act->attr);
                maybe_export_identifier_rhs_patch(thisAgent, state, symbol_map, prod->name->sc->name, action_index, soar::kernel::PRODUCTION_RHS_FIELD_VALUE, act->value);
                if (preference_is_binary(act->preference_type))
                {
                    maybe_export_identifier_rhs_patch(thisAgent, state, symbol_map, prod->name->sc->name, action_index, soar::kernel::PRODUCTION_RHS_FIELD_REFERENT, act->referent);
                }
            }
        }
    }
}

ProductionMap build_production_name_map(agent* thisAgent)
{
    ProductionMap production_map;
    for (int production_type = 0; production_type < NUM_PRODUCTION_TYPES; ++production_type)
    {
        for (production* prod = thisAgent->all_productions_of_type[production_type]; prod != NIL; prod = prod->next)
        {
            if (prod->name && prod->name->is_string())
            {
                production_map.emplace(prod->name->sc->name, prod);
            }
        }
    }
    return production_map;
}
