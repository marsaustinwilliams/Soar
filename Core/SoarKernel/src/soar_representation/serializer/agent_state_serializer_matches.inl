bool production_has_instantiation_for_match(production* prod, token* tok, wme* w)
{
    for (instantiation* inst = prod->instantiations; inst != NIL; inst = inst->next)
    {
        if ((inst->rete_token == tok) && (inst->rete_wme == w))
        {
            return true;
        }
    }

    return false;
}

bool pending_assertion_exists_for_match(ms_change* assertions, rete_node* p_node, token* tok, wme* w)
{
    for (ms_change* current = assertions; current != NIL; current = current->next)
    {
        if ((current->p_node == p_node) && (current->tok == tok) && (current->w == w))
        {
            return true;
        }
    }

    return false;
}

bool match_is_already_pending(agent* thisAgent, rete_node* p_node, token* tok, wme* w)
{
    return pending_assertion_exists_for_match(thisAgent->ms_i_assertions, p_node, tok, w)
        || pending_assertion_exists_for_match(thisAgent->ms_o_assertions, p_node, tok, w)
        || pending_assertion_exists_for_match(thisAgent->postponed_assertions, p_node, tok, w);
}

template <typename TimetagList>
bool instantiation_condition_timetags_equal(instantiation* inst, const TimetagList& timetags)
{
    size_t timetag_index = 0;
    for (condition* cond = inst ? inst->top_of_instantiated_conditions : NIL; cond != NIL; cond = cond->next)
    {
        if ((cond->type != POSITIVE_CONDITION) || !cond->bt.wme_)
        {
            continue;
        }

        if (timetag_index >= static_cast<size_t>(timetags.size()))
        {
            return false;
        }

        if (cond->bt.wme_->timetag != timetags[static_cast<int>(timetag_index)])
        {
            return false;
        }

        ++timetag_index;
    }

    return (timetag_index == static_cast<size_t>(timetags.size()));
}

void remove_detached_instantiated_conditions(agent* thisAgent, instantiation* inst)
{
    if (!thisAgent || !inst || !inst->top_of_instantiated_conditions)
    {
        return;
    }

    condition* kept_head = NIL;
    condition* kept_tail = NIL;
    condition* removed_head = NIL;
    condition* removed_tail = NIL;

    for (condition* cond = inst->top_of_instantiated_conditions; cond != NIL;)
    {
        condition* next_cond = cond->next;
        cond->next = NIL;
        cond->prev = NIL;

        const bool is_detached_condition =
            (cond->type == POSITIVE_CONDITION) && cond->bt.wme_ && !wme_is_in_rete(thisAgent, cond->bt.wme_);

        if (is_detached_condition)
        {
            if (removed_tail)
            {
                removed_tail->next = cond;
                cond->prev = removed_tail;
            }
            else
            {
                removed_head = cond;
            }
            removed_tail = cond;
        }
        else
        {
            if (kept_tail)
            {
                kept_tail->next = cond;
                cond->prev = kept_tail;
            }
            else
            {
                kept_head = cond;
            }
            kept_tail = cond;
        }

        cond = next_cond;
    }

    inst->top_of_instantiated_conditions = kept_head;
    inst->bottom_of_instantiated_conditions = kept_tail;

    if (removed_head)
    {
        for (condition* removed_cond = removed_head; removed_cond != NIL; removed_cond = removed_cond->next)
        {
            if ((removed_cond->type == POSITIVE_CONDITION) && removed_cond->bt.wme_)
            {
                wme_remove_ref(thisAgent, removed_cond->bt.wme_);
            }
        }
        deallocate_condition_list(thisAgent, removed_head);
    }
}

bool instantiation_matches_live_entry(instantiation* inst,
                                      const soar::kernel::MatchInstantiation& entry)
{
    if (!inst)
    {
        return false;
    }

    if ((entry.timetags_size() > 0) && instantiation_condition_timetags_equal(inst, entry.timetags()))
    {
        return true;
    }

    if ((entry.instantiated_condition_timetags_size() > 0) &&
        instantiation_condition_timetags_equal(inst, entry.instantiated_condition_timetags()))
    {
        return true;
    }

    return false;
}

instantiation* find_existing_instantiation_for_live_entry(production* prod,
                                                          const soar::kernel::MatchInstantiation& entry)
{
    for (instantiation* inst = prod ? prod->instantiations : NIL; inst != NIL; inst = inst->next)
    {
        if (instantiation_matches_live_entry(inst, entry))
        {
            return inst;
        }
    }

    return NIL;
}

void reify_current_matches_for_fired_productions(agent* thisAgent,
                                                 const SavedProductionFiringCountMap& saved_firing_counts)
{
    for (int production_type = 0; production_type < NUM_PRODUCTION_TYPES; ++production_type)
    {
        for (production* prod = thisAgent->all_productions_of_type[production_type]; prod != NIL; prod = prod->next)
        {
            if (!prod->p_node || !production_had_saved_firings(prod, saved_firing_counts))
            {
                continue;
            }

            for (token* pnode_tok = prod->p_node->a.np.tokens; pnode_tok != NIL; pnode_tok = pnode_tok->next_of_node)
            {
                token* match_tok = pnode_tok->parent;
                wme* match_wme = pnode_tok->w;

                if (!match_tok || production_has_instantiation_for_match(prod, match_tok, match_wme))
                {
                    continue;
                }

                ms_change temp_msc;
                temp_msc.tok = match_tok;
                temp_msc.w = match_wme;
                temp_msc.p_node = prod->p_node;

                Symbol* match_goal = find_goal_for_match_set_change_assertion(thisAgent, &temp_msc);
                if (!match_goal)
                {
                    continue;
                }

                create_refracted_instantiation(thisAgent,
                                               prod,
                                               match_tok,
                                               match_wme,
                                               match_goal,
                                               match_goal->id->level);
            }
        }
    }
}

bool restore_live_match_instantiations(agent* thisAgent,
                                       const soar::kernel::AgentState& state,
                                       const SymbolReverseMap& symbol_map,
                                       RestoredPreferenceMap& preference_map,
                                       RestoredPreferenceCloneMap& preference_clone_map,
                                       std::vector<instantiation*>* restored_instantiations = nullptr,
                                       std::unordered_map<uint64_t, instantiation*>* restored_instantiation_id_map = nullptr)
{
    const bool debug_live_match_delta = (std::getenv("SOAR_DEBUG_LIVE_MATCH_DELTA") != nullptr);
    const bool debug_live_match_ref_drift = (std::getenv("SOAR_DEBUG_LIVE_MATCH_REF_DRIFT") != nullptr);
    struct TrackedRefId
    {
        char letter;
        uint64_t number;
    };
    const TrackedRefId tracked_ref_ids[] = {
        {'I', 2},
        {'I', 3},
        {'J', 1},
        {'J', 2},
        {'O', 1},
        {'O', 2},
        {'O', 3},
        {'O', 4},
        {'O', 5},
        {'S', 1},
    };
    bool suppress_live_match_refire = true;
    if (const char* allow_refire_env = std::getenv("SOAR_RESTORE_ALLOW_LIVE_MATCH_REFIRE"))
    {
        if ((allow_refire_env[0] == '1') ||
            !std::strcmp(allow_refire_env, "true") ||
            !std::strcmp(allow_refire_env, "on"))
        {
            suppress_live_match_refire = false;
        }
    }

    bool skip_live_match_pref_replay = false;
    if (const char* skip_pref_replay_env = std::getenv("SOAR_RESTORE_SKIP_LIVE_MATCH_PREF_REPLAY"))
    {
        if ((skip_pref_replay_env[0] == '1') ||
            !std::strcmp(skip_pref_replay_env, "true") ||
            !std::strcmp(skip_pref_replay_env, "on"))
        {
            skip_live_match_pref_replay = true;
        }
    }

    size_t missing_matches = 0;
    size_t restored_live_instantiations = 0;
    size_t restored_nonlive_instantiations = 0;
    SavedInstantiationSet cleaned_previous_insts;

    auto replay_saved_preferences = [&](const soar::kernel::MatchInstantiation& live_entry,
                                        instantiation* target_inst)
    {
        if (!target_inst || skip_live_match_pref_replay)
        {
            return;
        }

        for (const auto& pref_entry : live_entry.preferences_generated())
        {
            Symbol* pref_id = symbol_by_id(symbol_map, pref_entry.id());
            Symbol* pref_attr = symbol_by_id(symbol_map, pref_entry.attr());
            Symbol* pref_value = symbol_by_id(symbol_map, pref_entry.value());
            if (!pref_id || !pref_attr || !pref_value)
            {
                continue;
            }

            preference* pref = get_or_create_restored_preference(thisAgent,
                                                                 pref_entry,
                                                                 pref_id,
                                                                 pref_attr,
                                                                 pref_value,
                                                                 symbol_map,
                                                                 preference_map,
                                                                 preference_clone_map);
            reassign_restored_preference_instantiation(thisAgent, pref, target_inst, &cleaned_previous_insts);
        }
    };

    auto create_placeholder_live_instantiation = [&](production* prod,
                                                     const soar::kernel::MatchInstantiation& live_entry) -> instantiation*
    {
        Symbol* match_goal = goal_for_level(thisAgent, static_cast<goal_stack_level>(live_entry.match_goal_level()));
        if (!match_goal)
        {
            match_goal = thisAgent->top_goal;
        }
        if (!match_goal)
        {
            return NIL;
        }

        instantiation* inst = NIL;
        init_instantiation(thisAgent,
                           inst,
                           thisAgent->symbolManager->soarSymbols.architecture_inst_symbol,
                           prod,
                           NIL,
                           NIL);
        inst->in_ms = live_entry.in_ms();
        inst->match_goal = match_goal;
        inst->match_goal_level = match_goal->id->level;

        return inst;
    };

    std::vector<const soar::kernel::MatchInstantiation*> sorted_live_match_entries;
    sorted_live_match_entries.reserve(static_cast<size_t>(state.live_match_instantiations_size()));
    for (const auto& entry : state.live_match_instantiations())
    {
        sorted_live_match_entries.push_back(&entry);
    }

    auto timetag_key = [](const soar::kernel::MatchInstantiation& e) -> uint64_t
    {
        if (e.timetags_size() > 0)
        {
            return static_cast<uint64_t>(e.timetags(0));
        }
        if (e.instantiated_condition_timetags_size() > 0)
        {
            return static_cast<uint64_t>(e.instantiated_condition_timetags(0));
        }
        if (e.instantiated_condition_wmes_size() > 0)
        {
            return static_cast<uint64_t>(e.instantiated_condition_wmes(0).timetag());
        }
        if (e.instantiated_conditions_size() > 0)
        {
            return static_cast<uint64_t>(e.instantiated_conditions(0).wme().timetag());
        }
        return 0;
    };

    std::stable_sort(sorted_live_match_entries.begin(), sorted_live_match_entries.end(),
                     [&](const soar::kernel::MatchInstantiation* lhs,
                         const soar::kernel::MatchInstantiation* rhs)
                     {
                         if (lhs->in_ms() != rhs->in_ms())
                         {
                             return lhs->in_ms() > rhs->in_ms();
                         }
                         if (lhs->production_name() != rhs->production_name())
                         {
                             return lhs->production_name() < rhs->production_name();
                         }
                         if (lhs->match_goal_level() != rhs->match_goal_level())
                         {
                             return lhs->match_goal_level() < rhs->match_goal_level();
                         }

                         const uint64_t lhs_timetag_key = timetag_key(*lhs);
                         const uint64_t rhs_timetag_key = timetag_key(*rhs);
                         if (lhs_timetag_key != rhs_timetag_key)
                         {
                             return lhs_timetag_key < rhs_timetag_key;
                         }

                         return lhs->instantiation_id() < rhs->instantiation_id();
                     });

    for (const soar::kernel::MatchInstantiation* entry_ptr : sorted_live_match_entries)
    {
        const auto& entry = *entry_ptr;
        auto get_identifier_refcount = [&](char letter, uint64_t number) -> uint64_t
        {
            Symbol* sym = thisAgent->symbolManager->find_identifier(letter, number);
            return (sym && sym->is_sti()) ? sym->reference_count : 0;
        };

        uint64_t tracked_ref_before[sizeof(tracked_ref_ids) / sizeof(tracked_ref_ids[0])] = {0};
        if (debug_live_match_ref_drift)
        {
            for (size_t i = 0; i < (sizeof(tracked_ref_ids) / sizeof(tracked_ref_ids[0])); ++i)
            {
                tracked_ref_before[i] = get_identifier_refcount(tracked_ref_ids[i].letter,
                                                                tracked_ref_ids[i].number);
            }
        }

        auto emit_live_match_ref_drift = [&](const char* stage)
        {
            if (!debug_live_match_ref_drift)
            {
                return;
            }

            bool any_change = false;
            std::fprintf(stderr,
                         "restore_live_match_ref_drift: stage=%s prod=%s in_ms=%d",
                         stage,
                         entry.production_name().c_str(),
                         entry.in_ms() ? 1 : 0);

            for (size_t i = 0; i < (sizeof(tracked_ref_ids) / sizeof(tracked_ref_ids[0])); ++i)
            {
                const uint64_t after_ref = get_identifier_refcount(tracked_ref_ids[i].letter,
                                                                   tracked_ref_ids[i].number);
                const long long delta = static_cast<long long>(after_ref) - static_cast<long long>(tracked_ref_before[i]);
                if (delta == 0)
                {
                    continue;
                }

                any_change = true;
                std::fprintf(stderr,
                             " %c%llu=%llu(%+lld)",
                             tracked_ref_ids[i].letter,
                             static_cast<unsigned long long>(tracked_ref_ids[i].number),
                             static_cast<unsigned long long>(after_ref),
                             delta);
            }

            if (!any_change)
            {
                std::fprintf(stderr, " no-change");
            }
            std::fprintf(stderr, "\n");
        };

        const uint64_t i4_ref_before = debug_live_match_delta ? get_identifier_refcount('I', 4) : 0;
        const uint64_t j1_ref_before = debug_live_match_delta ? get_identifier_refcount('J', 1) : 0;
        const uint64_t o2_ref_before = debug_live_match_delta ? get_identifier_refcount('O', 2) : 0;
        const uint64_t s1_ref_before = debug_live_match_delta ? get_identifier_refcount('S', 1) : 0;

        production* prod = NIL;
        for (int production_type = 0; production_type < NUM_PRODUCTION_TYPES && !prod; ++production_type)
        {
            for (production* current = thisAgent->all_productions_of_type[production_type]; current != NIL; current = current->next)
            {
                if (current->name && current->name->is_string() && entry.production_name() == current->name->sc->name)
                {
                    prod = current;
                    break;
                }
            }
        }

        if (!prod || !prod->p_node)
        {
            if ((entry.production_name() == "prefer*scottie") ||
                (entry.production_name() == "propose1") ||
                (entry.production_name() == "apply-op1") ||
                (entry.production_name() == "propose*stage*2") ||
                (entry.production_name() == "make-chunk"))
            {
                std::fprintf(stderr,
                             "restore_instantiation_missing_production: %s\n",
                             entry.production_name().c_str());
            }
            ++missing_matches;
            continue;
        }

        if (!entry.in_ms() && suppress_live_match_refire)
        {
            if (debug_live_match_delta)
            {
                std::fprintf(stderr,
                             "restore_live_match_defer_nonlive: %s in_ms=0\n",
                             entry.production_name().c_str());
            }

            /* Do not early-skip non-live entries: allow saved-condition fallback
               below to rebuild their instantiation structure without refiring. */
        }

        const bool has_saved_condition_payload =
            (entry.instantiated_conditions_size() > 0) ||
            (entry.instantiated_condition_wmes_size() > 0) ||
            (entry.instantiated_condition_timetags_size() > 0);

        bool restored = false;
        instantiation* restored_inst_for_entry = NIL;
        instantiation* matched_existing_inst = find_existing_instantiation_for_live_entry(prod, entry);
        if (matched_existing_inst)
        {
            matched_existing_inst->in_ms = entry.in_ms();
            replay_saved_preferences(entry, matched_existing_inst);
            emit_live_match_ref_drift("matched-existing");
            restored_inst_for_entry = matched_existing_inst;

            if (entry.in_ms())
            {
                ++restored_live_instantiations;
            }
            else
            {
                ++restored_nonlive_instantiations;
            }

            if (restored_instantiations)
            {
                restored_instantiations->push_back(matched_existing_inst);
            }

            restored = true;
        }

        if (!restored && entry.in_ms() && suppress_live_match_refire)
        {
            instantiation* placeholder_inst = create_placeholder_live_instantiation(prod, entry);
            replay_saved_preferences(entry, placeholder_inst);
            emit_live_match_ref_drift("placeholder-reuse-only");
            restored_inst_for_entry = placeholder_inst;

            if (placeholder_inst && restored_instantiations)
            {
                restored_instantiations->push_back(placeholder_inst);
            }

            if (debug_live_match_delta)
            {
                std::fprintf(stderr,
                             "restore_live_match_reuse_only: %s in_ms=1\n",
                             entry.production_name().c_str());
            }

            ++restored_live_instantiations;
            restored = true;
        }

        if (!restored && entry.in_ms() && !has_saved_condition_payload)
        {
            for (token* pnode_tok = prod->p_node->a.np.tokens; pnode_tok != NIL; pnode_tok = pnode_tok->next_of_node)
            {
                token* match_tok = pnode_tok->parent;
                wme* match_wme = pnode_tok->w;

                if (!match_tok || production_has_instantiation_for_match(prod, match_tok, match_wme))
                {
                    continue;
                }

                if (!match_timetags_equal(thisAgent, match_tok, match_wme, entry.timetags()))
                {
                    continue;
                }

                if (match_is_already_pending(thisAgent, prod->p_node, match_tok, match_wme))
                {
                    restored = true;
                    break;
                }

                Symbol* match_goal = goal_for_level(thisAgent, static_cast<goal_stack_level>(entry.match_goal_level()));
                if (!match_goal)
                {
                    ms_change temp_msc;
                    temp_msc.tok = match_tok;
                    temp_msc.w = match_wme;
                    temp_msc.p_node = prod->p_node;
                    match_goal = find_goal_for_match_set_change_assertion(thisAgent, &temp_msc);
                }

                if (!match_goal)
                {
                    continue;
                }

                instantiation* restored_inst = create_refracted_instantiation(thisAgent,
                                                                              prod,
                                                                              match_tok,
                                                                              match_wme,
                                                                              match_goal,
                                                                              match_goal->id->level,
                                                                              entry.in_ms(),
                                                                              false);
                if (!restored_inst)
                {
                    continue;
                }

                if (restored_inst->preferences_generated)
                {
                    for (preference* generated_pref = restored_inst->preferences_generated;
                         generated_pref != NIL;)
                    {
                        preference* next_generated_pref = generated_pref->inst_next;
                        deallocate_preference(thisAgent, generated_pref, true);
                        generated_pref = next_generated_pref;
                    }
                    restored_inst->preferences_generated = NIL;
                }

                restore_instantiated_condition_identities(thisAgent,
                                                         restored_inst,
                                                         match_goal,
                                                         entry.instantiated_conditions());
                if ((entry.production_name() == "prefer*scottie") ||
                    (entry.production_name() == "propose1") ||
                    (entry.production_name() == "apply-op1") ||
                    (entry.production_name() == "propose*stage*2") ||
                    (entry.production_name() == "make-chunk"))
                {
                    std::fprintf(stderr,
                                 "restore_instantiation: %s in_ms=%d\n",
                                 entry.production_name().c_str(),
                                 entry.in_ms() ? 1 : 0);
                }
                replay_saved_preferences(entry, restored_inst);
                emit_live_match_ref_drift("create-refracted");
                restored_inst_for_entry = restored_inst;

                restored_inst->OSK_prefs = NIL;
                restored_inst->OSK_proposal_prefs = NIL;
                restored_inst->OSK_proposal_slot = NIL;

                if ((match_goal->id->level > TOP_GOAL_LEVEL) && thisAgent->explanationBasedChunker->ebc_settings[SETTING_EBC_LEARNING_ON])
                {
                    thisAgent->explanationBasedChunker->clear_id_to_identity_map();
                }
                else if (prod->type != TEMPLATE_PRODUCTION_TYPE)
                {
                    thisAgent->explanationBasedChunker->clear_symbol_identity_map();
                }
                restored = true;
                if (entry.in_ms())
                {
                    ++restored_live_instantiations;
                }
                else
                {
                    ++restored_nonlive_instantiations;
                }

                if (restored_instantiations)
                {
                    restored_instantiations->push_back(restored_inst);
                }
                break;
            }
        }

        if (!restored)
        {
            ++missing_matches;
        }

        if (!restored && ((entry.instantiated_condition_wmes_size() > 0) || (entry.instantiated_condition_timetags_size() > 0)))
        {
            Symbol* match_goal = goal_for_level(thisAgent, static_cast<goal_stack_level>(entry.match_goal_level()));
            if ((entry.production_name() == "apply-op1") || (entry.production_name() == "prefer*scottie"))
            {
                std::fprintf(stderr,
                             "restore_saved_condition_attempt: %s conds=%d wmes=%d goal=%s\n",
                             entry.production_name().c_str(),
                             entry.instantiated_condition_timetags_size(),
                             entry.instantiated_condition_wmes_size(),
                             match_goal ? match_goal->to_string(true, false, NIL, 0) : "<nil>");
            }
            if (match_goal)
            {
                instantiation* restored_inst = (entry.instantiated_condition_wmes_size() > 0)
                    ? ((entry.instantiated_conditions_size() > 0)
                        ? create_saved_condition_instantiation(thisAgent,
                                                               prod,
                                                               match_goal,
                                                               match_goal->id->level,
                                                               entry.instantiated_conditions(),
                                                               symbol_map,
                                                               preference_map,
                                                               preference_clone_map,
                                                               entry.in_ms())
                        : create_saved_condition_instantiation(thisAgent,
                                                               prod,
                                                               match_goal,
                                                               match_goal->id->level,
                                                               entry.instantiated_condition_wmes(),
                                                               symbol_map,
                                                               preference_map,
                                                               preference_clone_map,
                                                               entry.in_ms()))
                    : create_saved_condition_instantiation(thisAgent,
                                                           prod,
                                                           match_goal,
                                                           match_goal->id->level,
                                                           entry.instantiated_condition_timetags(),
                                                           entry.in_ms());
                if (restored_inst)
                {
                    uint64_t cond_count = 0;
                    uint64_t detached_cond_count = 0;
                    uint64_t attached_cond_count = 0;
                    for (condition* cond = restored_inst->top_of_instantiated_conditions; cond != NIL; cond = cond->next)
                    {
                        if ((cond->type != POSITIVE_CONDITION) || !cond->bt.wme_)
                        {
                            continue;
                        }

                        ++cond_count;
                        if (wme_is_in_rete(thisAgent, cond->bt.wme_))
                        {
                            ++attached_cond_count;
                        }
                        else
                        {
                            ++detached_cond_count;
                        }
                    }

                    if (std::getenv("SOAR_DEBUG_SAVED_COND_DETACHED"))
                    {
                        std::fprintf(stderr,
                                     "restore_saved_condition_detached: %s in_ms=%d conds=%llu attached=%llu detached=%llu\n",
                                     entry.production_name().c_str(),
                                     entry.in_ms() ? 1 : 0,
                                     static_cast<unsigned long long>(cond_count),
                                     static_cast<unsigned long long>(attached_cond_count),
                                     static_cast<unsigned long long>(detached_cond_count));
                    }

                    bool skip_nonlive_attached_only = false;
                    if (const char* skip_attached_only_env = std::getenv("SOAR_RESTORE_SKIP_NONLIVE_ATTACHED_ONLY"))
                    {
                        if ((skip_attached_only_env[0] == '1') ||
                            !std::strcmp(skip_attached_only_env, "true") ||
                            !std::strcmp(skip_attached_only_env, "on"))
                        {
                            skip_nonlive_attached_only = true;
                        }
                    }

                    bool skip_nonlive_mostly_attached = false;
                    if (const char* skip_mostly_attached_env = std::getenv("SOAR_RESTORE_SKIP_NONLIVE_MOSTLY_ATTACHED"))
                    {
                        if ((skip_mostly_attached_env[0] == '0') ||
                            !std::strcmp(skip_mostly_attached_env, "false") ||
                            !std::strcmp(skip_mostly_attached_env, "off"))
                        {
                            skip_nonlive_mostly_attached = false;
                        }
                        else if ((skip_mostly_attached_env[0] == '1') ||
                                 !std::strcmp(skip_mostly_attached_env, "true") ||
                                 !std::strcmp(skip_mostly_attached_env, "on"))
                        {
                            skip_nonlive_mostly_attached = true;
                        }
                    }

                    if (skip_nonlive_attached_only && !entry.in_ms())
                    {
                        if (detached_cond_count == 0)
                        {
                            deallocate_instantiation(thisAgent, restored_inst);
                            restored_inst = NIL;
                            continue;
                        }
                    }

                    if (skip_nonlive_mostly_attached && !entry.in_ms() && (detached_cond_count < attached_cond_count))
                    {
                        deallocate_instantiation(thisAgent, restored_inst);
                        restored_inst = NIL;
                        continue;
                    }

                    emit_live_match_ref_drift("create-saved-condition-built-inst");
                    if ((entry.production_name() == "prefer*scottie") ||
                        (entry.production_name() == "propose1") ||
                        (entry.production_name() == "apply-op1") ||
                        (entry.production_name() == "propose*stage*2") ||
                        (entry.production_name() == "make-chunk"))
                    {
                        std::fprintf(stderr,
                                     "restore_saved_condition_instantiation: %s in_ms=%d conds=%d\n",
                                     entry.production_name().c_str(),
                                     entry.in_ms() ? 1 : 0,
                                     entry.instantiated_condition_timetags_size());
                    }

                    replay_saved_preferences(entry, restored_inst);
                    emit_live_match_ref_drift("create-saved-condition-post-replay");

                    if (!entry.in_ms())
                    {
                        remove_detached_instantiated_conditions(thisAgent, restored_inst);
                        emit_live_match_ref_drift("create-saved-condition-post-clear-detached-only");
                    }

                    restored_inst_for_entry = restored_inst;

                    restored_inst->OSK_prefs = NIL;
                    restored_inst->OSK_proposal_prefs = NIL;
                    restored_inst->OSK_proposal_slot = NIL;

                    if (entry.in_ms())
                    {
                        ++restored_live_instantiations;
                    }
                    else
                    {
                        ++restored_nonlive_instantiations;
                    }

                    if (restored_instantiations)
                    {
                        restored_instantiations->push_back(restored_inst);
                    }

                    restored = true;
                }
            }
        }

        if (!restored && ((entry.production_name() == "prefer*scottie") ||
                          (entry.production_name() == "propose1") ||
                          (entry.production_name() == "apply-op1") ||
                          (entry.production_name() == "propose*stage*2") ||
                          (entry.production_name() == "make-chunk")))
        {
            std::fprintf(stderr,
                         "restore_instantiation_no_match: %s timetags=%d conds=%d wmes=%d\n",
                         entry.production_name().c_str(),
                         entry.timetags_size(),
                         entry.instantiated_condition_timetags_size(),
                         entry.instantiated_condition_wmes_size());
        }

        if (debug_live_match_delta && restored)
        {
            const uint64_t i4_ref_after = get_identifier_refcount('I', 4);
            const uint64_t j1_ref_after = get_identifier_refcount('J', 1);
            const uint64_t o2_ref_after = get_identifier_refcount('O', 2);
            const uint64_t s1_ref_after = get_identifier_refcount('S', 1);

            if ((i4_ref_after != i4_ref_before) ||
                (j1_ref_after != j1_ref_before) ||
                (o2_ref_after != o2_ref_before) ||
                (s1_ref_after != s1_ref_before))
            {
                std::fprintf(stderr,
                             "restore_live_match_delta: %s in_ms=%d I4=%lld J1=%lld O2=%lld S1=%lld\n",
                             entry.production_name().c_str(),
                             entry.in_ms() ? 1 : 0,
                             static_cast<long long>(i4_ref_after) - static_cast<long long>(i4_ref_before),
                             static_cast<long long>(j1_ref_after) - static_cast<long long>(j1_ref_before),
                             static_cast<long long>(o2_ref_after) - static_cast<long long>(o2_ref_before),
                             static_cast<long long>(s1_ref_after) - static_cast<long long>(s1_ref_before));
            }
        }

        if (restored && restored_inst_for_entry && restored_instantiation_id_map)
        {
            auto map_saved_inst_id = [&](uint64_t saved_inst_id)
            {
                if (saved_inst_id)
                {
                    (*restored_instantiation_id_map)[saved_inst_id] = restored_inst_for_entry;
                }
            };

            map_saved_inst_id(entry.instantiation_id());

            for (const auto& condition_entry : entry.instantiated_conditions())
            {
                map_saved_inst_id(condition_entry.wme().supporting_instantiation_id());
                map_saved_inst_id(condition_entry.wme().creating_instantiation_id());
            }

            for (const auto& condition_wme : entry.instantiated_condition_wmes())
            {
                map_saved_inst_id(condition_wme.supporting_instantiation_id());
                map_saved_inst_id(condition_wme.creating_instantiation_id());
            }
        }
    }

    if (missing_matches)
    {
        thisAgent->outputManager->printa_sf(thisAgent,
                            "Warning: %u saved live matches could not be restored exactly.\n",
                            static_cast<uint64_t>(missing_matches));
    }

    return missing_matches == 0;
}

void reify_pending_matches(agent* thisAgent)
{
    Symbol* original_active_goal = thisAgent->active_goal;
    goal_stack_level original_active_level = thisAgent->active_level;
    auto original_firing_type = thisAgent->FIRING_TYPE;

    for (Symbol* goal = thisAgent->top_goal; goal != NIL; goal = goal->id->lower_goal)
    {
        thisAgent->active_goal = goal;
        thisAgent->active_level = goal->id->level;

        for (auto firing_type : {IE_PRODS, PE_PRODS})
        {
            thisAgent->FIRING_TYPE = firing_type;

            production* prod = NIL;
            token* tok = NIL;
            wme* w = NIL;
            while (postpone_assertion(thisAgent, &prod, &tok, &w))
            {
                if (prod != NIL)
                {
                    create_refracted_instantiation(thisAgent, prod, tok, w, goal, goal->id->level);
                }
                consume_last_postponed_assertion(thisAgent);
            }
        }
    }

    thisAgent->FIRING_TYPE = original_firing_type;
    thisAgent->active_goal = original_active_goal;
    thisAgent->active_level = original_active_level;
}

void reset_loaded_production_firing_counts(agent* thisAgent)
{
    for (int production_type = 0; production_type < NUM_PRODUCTION_TYPES; ++production_type)
    {
        for (production* prod = thisAgent->all_productions_of_type[production_type]; prod != NIL; prod = prod->next)
        {
            prod->firing_count = 0;
        }
    }
}

void apply_production_firing_counts(agent* thisAgent,
                                    const soar::kernel::AgentState& state)
{
    if (state.production_firing_counts_size() == 0)
    {
        return;
    }

    const bool debug_pre_reset_firing_delta = (std::getenv("SOAR_DEBUG_PRE_RESET_FC_DELTA") != nullptr);
    if (debug_pre_reset_firing_delta)
    {
        SavedProductionFiringCountMap saved_firing_counts;
        saved_firing_counts.reserve(static_cast<size_t>(state.production_firing_counts_size()));
        for (const auto& entry : state.production_firing_counts())
        {
            saved_firing_counts.emplace(entry.production_name(), entry.firing_count());
        }

        uint64_t changed_counts = 0;
        uint64_t increased_counts = 0;

        for (int production_type = 0; production_type < NUM_PRODUCTION_TYPES; ++production_type)
        {
            for (production* prod = thisAgent->all_productions_of_type[production_type]; prod != NIL; prod = prod->next)
            {
                if (!prod->name || !prod->name->is_string())
                {
                    continue;
                }

                auto saved_it = saved_firing_counts.find(prod->name->sc->name);
                if (saved_it == saved_firing_counts.end())
                {
                    continue;
                }

                const uint64_t current_count = prod->firing_count;
                const uint64_t saved_count = saved_it->second;
                if (current_count == saved_count)
                {
                    continue;
                }

                ++changed_counts;
                if (current_count > saved_count)
                {
                    ++increased_counts;
                }

                std::fprintf(stderr,
                             "restore_pre_reset_fc_delta: %s saved=%llu current=%llu delta=%lld\n",
                             prod->name->sc->name,
                             static_cast<unsigned long long>(saved_count),
                             static_cast<unsigned long long>(current_count),
                             static_cast<long long>(current_count) - static_cast<long long>(saved_count));
            }
        }

        std::fprintf(stderr,
                     "restore_pre_reset_fc_summary: changed=%llu increased=%llu\n",
                     static_cast<unsigned long long>(changed_counts),
                     static_cast<unsigned long long>(increased_counts));
    }

    reset_loaded_production_firing_counts(thisAgent);

    size_t missing_counts = 0;
    for (const auto& entry : state.production_firing_counts())
    {
        bool found = false;
        for (int production_type = 0; production_type < NUM_PRODUCTION_TYPES && !found; ++production_type)
        {
            for (production* prod = thisAgent->all_productions_of_type[production_type]; prod != NIL; prod = prod->next)
            {
                if (prod->name && prod->name->is_string() && entry.production_name() == prod->name->sc->name)
                {
                    prod->firing_count = entry.firing_count();
                    found = true;
                    break;
                }
            }
        }

        if (!found)
        {
            ++missing_counts;
        }
    }

    if (missing_counts)
    {
        thisAgent->outputManager->printa_sf(thisAgent,
                            "Warning: %u saved production firing counts could not be matched during restore.\n",
                            static_cast<uint64_t>(missing_counts));
    }
}

SavedProductionFiringCountMap build_saved_production_firing_count_map(const soar::kernel::AgentState& state)
{
    SavedProductionFiringCountMap saved_firing_counts;
    saved_firing_counts.reserve(static_cast<size_t>(state.production_firing_counts_size()));
    for (const auto& entry : state.production_firing_counts())
    {
        saved_firing_counts.emplace(entry.production_name(), entry.firing_count());
    }

    return saved_firing_counts;
}