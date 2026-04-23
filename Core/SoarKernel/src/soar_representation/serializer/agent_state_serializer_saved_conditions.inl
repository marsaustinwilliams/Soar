bool symbol_is_in_cons_list(cons* head, Symbol* target)
{
    for (cons* current = head; current != NIL; current = current->rest)
    {
        if (current->first == target)
        {
            return true;
        }
    }

    return false;
}

bool match_timetags_equal(agent* thisAgent,
                          token* tok,
                          wme* w,
                          const std::vector<uint64_t>& saved_timetags)
{
    std::vector<uint64_t> current_timetags = capture_match_timetags(thisAgent, tok, w);
    return current_timetags == saved_timetags;
}

std::vector<uint64_t> capture_match_timetags(agent* /*thisAgent*/, token* tok, wme* w)
{
    std::vector<uint64_t> timetags;
    for (token* current = tok; current != NIL; current = current->parent)
    {
        if (current->w)
        {
            timetags.push_back(current->w->timetag);
        }
    }
    std::reverse(timetags.begin(), timetags.end());
    if (w && (timetags.empty() || (timetags.back() != w->timetag)))
    {
        timetags.push_back(w->timetag);
    }
    return timetags;
}

bool match_timetags_equal(agent* thisAgent,
                          token* tok,
                          wme* w,
                          const google::protobuf::RepeatedField<uint64_t>& saved_timetags)
{
    std::vector<uint64_t> current_timetags = capture_match_timetags(thisAgent, tok, w);
    if (current_timetags.size() != static_cast<size_t>(saved_timetags.size()))
    {
        return false;
    }

    for (int index = 0; index < saved_timetags.size(); ++index)
    {
        if (current_timetags[static_cast<size_t>(index)] != saved_timetags.Get(index))
        {
            return false;
        }
    }

    return true;
}

bool capture_instantiated_condition_timetags(instantiation* inst, std::vector<uint64_t>& timetags)
{
    timetags.clear();
    if (!inst)
    {
        return false;
    }

    for (condition* cond = inst->top_of_instantiated_conditions; cond != NIL; cond = cond->next)
    {
        if ((cond->type == POSITIVE_CONDITION) && cond->bt.wme_)
        {
            timetags.push_back(cond->bt.wme_->timetag);
        }
    }

    return !timetags.empty();
}

bool instantiation_has_only_detached_condition_wmes(agent* thisAgent, instantiation* inst)
{
    if (!inst)
    {
        return false;
    }

    bool saw_condition_wme = false;
    for (condition* cond = inst->top_of_instantiated_conditions; cond != NIL; cond = cond->next)
    {
        if ((cond->type != POSITIVE_CONDITION) || !cond->bt.wme_)
        {
            continue;
        }

        saw_condition_wme = true;
        if (wme_is_in_rete(thisAgent, cond->bt.wme_))
        {
            return false;
        }
    }

    return saw_condition_wme;
}

bool instantiation_has_any_detached_condition_wmes(agent* thisAgent, instantiation* inst)
{
    if (!inst)
    {
        return false;
    }

    for (condition* cond = inst->top_of_instantiated_conditions; cond != NIL; cond = cond->next)
    {
        if ((cond->type != POSITIVE_CONDITION) || !cond->bt.wme_)
        {
            continue;
        }

        if (!wme_is_in_rete(thisAgent, cond->bt.wme_))
        {
            return true;
        }
    }

    return false;
}

wme* find_restored_wme_by_timetag(agent* thisAgent, uint64_t timetag)
{
    for (wme* current = thisAgent->all_wmes_in_rete; current != NIL; current = current->rete_next)
    {
        if (current->timetag == timetag)
        {
            return current;
        }
    }

    return NIL;
}

uint64_t debug_identifier_refcount(agent* thisAgent, char letter, uint64_t number)
{
    if (!thisAgent || !thisAgent->symbolManager)
    {
        return 0;
    }

    Symbol* sym = thisAgent->symbolManager->find_identifier(letter, number);
    return (sym && sym->is_sti()) ? sym->reference_count : 0;
}

uint64_t debug_symbol_refcount_if_identifier(Symbol* sym)
{
    return (sym && sym->is_sti()) ? sym->reference_count : 0;
}

void debug_emit_symbol_ref_delta_if_identifier(const char* prod_name,
                                               const char* stage,
                                               uint64_t timetag,
                                               const char* field,
                                               Symbol* sym,
                                               uint64_t before)
{
    if (!(sym && sym->is_sti()))
    {
        return;
    }

    const uint64_t after = sym->reference_count;
    const long long delta = static_cast<long long>(after) - static_cast<long long>(before);
    if (delta == 0)
    {
        return;
    }

    std::fprintf(stderr,
                 "saved_condition_symbol_ref_drift: prod=%s stage=%s timetag=%llu field=%s id=%c%llu before=%llu after=%llu delta=%+lld\n",
                 prod_name,
                 stage,
                 static_cast<unsigned long long>(timetag),
                 field,
                 sym->id->name_letter,
                 static_cast<unsigned long long>(sym->id->name_number),
                 static_cast<unsigned long long>(before),
                 static_cast<unsigned long long>(after),
                 delta);
}

wme* create_saved_condition_wme(agent* thisAgent,
                                const soar::kernel::WmeEntry& wme_entry,
                                const SymbolReverseMap& symbol_map,
                                RestoredPreferenceMap& preference_map,
                                RestoredPreferenceCloneMap& preference_clone_map,
                                bool restore_preference)
{
    Symbol* id = symbol_by_id(symbol_map, wme_entry.id_symbol());
    Symbol* attr = symbol_by_id(symbol_map, wme_entry.attr_symbol());
    Symbol* value = symbol_by_id(symbol_map, wme_entry.value_symbol());
    if (!id || !attr || !value)
    {
        return NIL;
    }

    if (wme* existing = find_restored_wme_by_timetag(thisAgent, wme_entry.timetag()))
    {
        return existing;
    }

    wme* detached_wme = make_wme(thisAgent, id, attr, value, wme_entry.acceptable());
    detached_wme->timetag = wme_entry.timetag();
    detached_wme->preference = restore_preference
        ? get_or_create_restored_preference(thisAgent,
                                            wme_entry,
                                            id,
                                            attr,
                                            value,
                                            symbol_map,
                                            preference_map,
                                            preference_clone_map)
        : NIL;
    detached_wme->right_mems = NIL;
    detached_wme->tokens = NIL;
    return detached_wme;
}

void refresh_synthetic_instantiation_osk_from_slots(agent* thisAgent, instantiation* inst)
{
    if (!inst)
    {
        return;
    }

    inst->OSK_prefs = NIL;
    std::unordered_set<preference*> attached_osk_prefs;
    for (condition* cond = inst->top_of_instantiated_conditions; cond != NIL; cond = cond->next)
    {
        if ((cond->type != POSITIVE_CONDITION) || cond->test_for_acceptable_preference)
        {
            continue;
        }

        test id_test = cond->data.tests.id_test->eq_test;
        test attr_test = cond->data.tests.attr_test->eq_test;
        if (!id_test || !attr_test)
        {
            continue;
        }

        Symbol* id = id_test->data.referent;
        Symbol* attr = attr_test->data.referent;
        if (!id || !attr || !id->is_sti())
        {
            continue;
        }

        if (inst->prod_name && inst->prod_name->is_string() && !std::strcmp(inst->prod_name->sc->name, "apply-op1"))
        {
            std::string id_str;
            std::string attr_str;
            std::string value_str;
            thisAgent->outputManager->sprinta_sf(thisAgent, id_str, "%t", cond->data.tests.id_test);
            thisAgent->outputManager->sprinta_sf(thisAgent, attr_str, "%t", cond->data.tests.attr_test);
            thisAgent->outputManager->sprinta_sf(thisAgent, value_str, "%t", cond->data.tests.value_test);
            std::fprintf(stderr,
                         "  synthetic_slot_osk_scan: %s | %s | %s | slot_exists=%u slot_osk=%u\n",
                         id_str.c_str(),
                         attr_str.c_str(),
                         value_str.c_str(),
                         find_slot(id, attr) ? 1U : 0U,
                         (find_slot(id, attr) && find_slot(id, attr)->OSK_prefs) ? 1U : 0U);
        }

        if (id->id->level != inst->match_goal_level)
        {
            continue;
        }

        slot* s = find_slot(id, attr);
        if (!s || !s->OSK_prefs)
        {
            continue;
        }

        for (cons* osk_pref = s->OSK_prefs; osk_pref != NIL; osk_pref = osk_pref->rest)
        {
            preference* pref = static_cast<preference*>(osk_pref->first);
            if (!pref || !attached_osk_prefs.insert(pref).second)
            {
                continue;
            }
            push(thisAgent, pref, inst->OSK_prefs);
            preference_add_ref(pref);
        }
    }
}

instantiation* create_saved_condition_instantiation(agent* thisAgent,
                                                    production* prod,
                                                    Symbol* match_goal,
                                                    goal_stack_level match_level,
                                                    const google::protobuf::RepeatedField<uint64_t>& condition_timetags,
                                                    bool in_ms)
{
    if (!prod || !match_goal || (condition_timetags.size() == 0))
    {
        return NIL;
    }

    struct TrackedRefId
    {
        char letter;
        uint64_t number;
    };
    const TrackedRefId tracked_ref_ids[] = {
        {'J', 1},
        {'J', 2},
        {'O', 1},
        {'O', 2},
        {'O', 3},
        {'O', 4},
        {'S', 1},
    };

    const char* prod_name = (prod && prod->name && prod->name->is_string()) ? prod->name->sc->name : "<nil>";
    const bool debug_saved_condition_ref_drift =
        (std::getenv("SOAR_DEBUG_SAVED_COND_REF_DRIFT") != nullptr) &&
        (std::strncmp(prod_name, "apply*", 6) == 0);
    const bool debug_skip_detached_saved_condition_create =
        (!in_ms) && (std::getenv("SOAR_DEBUG_SKIP_DETACHED_SAVED_COND_AT_CREATE") != nullptr);

    uint64_t tracked_ref_before[sizeof(tracked_ref_ids) / sizeof(tracked_ref_ids[0])] = {0};
    if (debug_saved_condition_ref_drift)
    {
        for (size_t i = 0; i < (sizeof(tracked_ref_ids) / sizeof(tracked_ref_ids[0])); ++i)
        {
            tracked_ref_before[i] = debug_identifier_refcount(thisAgent,
                                                              tracked_ref_ids[i].letter,
                                                              tracked_ref_ids[i].number);
        }
    }

    auto emit_saved_condition_ref_drift = [&](const char* stage, uint64_t timetag)
    {
        if (!debug_saved_condition_ref_drift)
        {
            return;
        }

        bool any_change = false;
        std::fprintf(stderr,
                     "saved_condition_ref_drift: prod=%s stage=%s timetag=%llu",
                     prod_name,
                     stage,
                     static_cast<unsigned long long>(timetag));

        for (size_t i = 0; i < (sizeof(tracked_ref_ids) / sizeof(tracked_ref_ids[0])); ++i)
        {
            const uint64_t after_ref = debug_identifier_refcount(thisAgent,
                                                                 tracked_ref_ids[i].letter,
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

    instantiation* inst = NIL;
    init_instantiation(thisAgent,
                       inst,
                       (prod && prod->name) ? prod->name : thisAgent->symbolManager->soarSymbols.architecture_inst_symbol,
                       prod,
                       NIL,
                       NIL);
    emit_saved_condition_ref_drift("post-init", 0);
    inst->in_ms = in_ms;
    inst->match_goal = match_goal;
    inst->match_goal_level = match_level;
    condition* previous_cond = NIL;
    for (uint64_t timetag : condition_timetags)
    {
        wme* matched_wme = find_restored_wme_by_timetag(thisAgent, timetag);
        if (!matched_wme)
        {
            std::fprintf(stderr,
                         "create_saved_condition_instantiation_missing_wme: %s timetag=%llu\n",
                         (prod && prod->name && prod->name->is_string()) ? prod->name->sc->name : "<nil>",
                         static_cast<unsigned long long>(timetag));
            deallocate_instantiation(thisAgent, inst);
            return NIL;
        }

        if (debug_skip_detached_saved_condition_create && !wme_is_in_rete(thisAgent, matched_wme))
        {
            std::fprintf(stderr,
                         "create_saved_condition_instantiation_skip_detached_wme: %s timetag=%llu in_ms=%d\n",
                         prod_name,
                         static_cast<unsigned long long>(timetag),
                         in_ms ? 1 : 0);
            continue;
        }

        emit_saved_condition_ref_drift("pre-make-condition", timetag);

        const uint64_t id_ref_before = debug_symbol_refcount_if_identifier(matched_wme->id);
        const uint64_t attr_ref_before = debug_symbol_refcount_if_identifier(matched_wme->attr);
        const uint64_t value_ref_before = debug_symbol_refcount_if_identifier(matched_wme->value);

        condition* cond = make_condition(thisAgent,
                                         make_test(thisAgent, matched_wme->id, EQUALITY_TEST),
                                         make_test(thisAgent, matched_wme->attr, EQUALITY_TEST),
                                         make_test(thisAgent, matched_wme->value, EQUALITY_TEST));

        if (debug_saved_condition_ref_drift)
        {
            debug_emit_symbol_ref_delta_if_identifier(prod_name,
                                                      "post-make-condition",
                                                      timetag,
                                                      "id",
                                                      matched_wme->id,
                                                      id_ref_before);
            debug_emit_symbol_ref_delta_if_identifier(prod_name,
                                                      "post-make-condition",
                                                      timetag,
                                                      "attr",
                                                      matched_wme->attr,
                                                      attr_ref_before);
            debug_emit_symbol_ref_delta_if_identifier(prod_name,
                                                      "post-make-condition",
                                                      timetag,
                                                      "value",
                                                      matched_wme->value,
                                                      value_ref_before);
        }

        emit_saved_condition_ref_drift("post-make-condition", timetag);
        cond->inst = inst;
        cond->bt.wme_ = matched_wme;
        cond->bt.level = matched_wme->id->id->level;
        cond->bt.trace = matched_wme->preference;
        cond->test_for_acceptable_preference = matched_wme->acceptable;

        cond->prev = previous_cond;
        cond->next = NIL;
        if (previous_cond)
        {
            previous_cond->next = cond;
            inst->bottom_of_instantiated_conditions = cond;
        }
        else
        {
            inst->top_of_instantiated_conditions = cond;
            inst->bottom_of_instantiated_conditions = cond;
        }
        previous_cond = cond;

        emit_saved_condition_ref_drift("post-link-condition", timetag);
    }

    if (!inst->top_of_instantiated_conditions)
    {
        deallocate_instantiation(thisAgent, inst);
        return NIL;
    }

    finalize_instantiation(thisAgent, inst, false, NIL, false);
    emit_saved_condition_ref_drift("post-finalize", 0);

    if (inst->prod_name && inst->prod_name->is_string() && !std::strcmp(inst->prod_name->sc->name, "apply-op1"))
    {
        std::fprintf(stderr, "synthetic_finalize_instantiation apply-op1:\n");
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
                         "  cond: %s | %s | %s | trace=%u trace_level=%d | inst_ids=%llu/%llu/%llu | ids=%llu:%llu/%llu:%llu/%llu:%llu\n",
                         id_test.c_str(),
                         attr_test.c_str(),
                         value_test.c_str(),
                         debug_cond->bt.trace ? 1U : 0U,
                         debug_cond->bt.trace ? static_cast<int>(debug_cond->bt.trace->level) : -1,
                         static_cast<unsigned long long>(id_eq && id_eq->inst_identity ? id_eq->inst_identity : 0),
                         static_cast<unsigned long long>(attr_eq && attr_eq->inst_identity ? attr_eq->inst_identity : 0),
                         static_cast<unsigned long long>(value_eq && value_eq->inst_identity ? value_eq->inst_identity : 0),
                         static_cast<unsigned long long>(id_eq && id_eq->identity ? id_eq->identity->get_sub_identity() : 0),
                         static_cast<unsigned long long>(id_eq && id_eq->identity ? id_eq->identity->get_identity() : 0),
                         static_cast<unsigned long long>(attr_eq && attr_eq->identity ? attr_eq->identity->get_sub_identity() : 0),
                         static_cast<unsigned long long>(attr_eq && attr_eq->identity ? attr_eq->identity->get_identity() : 0),
                         static_cast<unsigned long long>(value_eq && value_eq->identity ? value_eq->identity->get_sub_identity() : 0),
                         static_cast<unsigned long long>(value_eq && value_eq->identity ? value_eq->identity->get_identity() : 0));
        }
    }

    return inst;
}

instantiation* create_saved_condition_instantiation(agent* thisAgent,
                                                    production* prod,
                                                    Symbol* match_goal,
                                                    goal_stack_level match_level,
                                                    const google::protobuf::RepeatedPtrField<soar::kernel::InstantiatedConditionEntry>& condition_entries,
                                                    const SymbolReverseMap& symbol_map,
                                                    RestoredPreferenceMap& preference_map,
                                                    RestoredPreferenceCloneMap& preference_clone_map,
                                                    bool in_ms)
{
    if (!prod || !match_goal || (condition_entries.size() == 0))
    {
        return NIL;
    }

    struct TrackedRefId
    {
        char letter;
        uint64_t number;
    };
    const TrackedRefId tracked_ref_ids[] = {
        {'J', 1},
        {'J', 2},
        {'O', 4},
        {'S', 1},
    };

    const char* prod_name = (prod && prod->name && prod->name->is_string()) ? prod->name->sc->name : "<nil>";
    const bool debug_saved_condition_ref_drift =
        (std::getenv("SOAR_DEBUG_SAVED_COND_REF_DRIFT") != nullptr) &&
        (std::strncmp(prod_name, "apply*", 6) == 0);
    const bool debug_skip_detached_saved_condition_create =
        (!in_ms) && (std::getenv("SOAR_DEBUG_SKIP_DETACHED_SAVED_COND_AT_CREATE") != nullptr);

    uint64_t tracked_ref_before[sizeof(tracked_ref_ids) / sizeof(tracked_ref_ids[0])] = {0};
    if (debug_saved_condition_ref_drift)
    {
        for (size_t i = 0; i < (sizeof(tracked_ref_ids) / sizeof(tracked_ref_ids[0])); ++i)
        {
            tracked_ref_before[i] = debug_identifier_refcount(thisAgent,
                                                              tracked_ref_ids[i].letter,
                                                              tracked_ref_ids[i].number);
        }
    }

    auto emit_saved_condition_ref_drift = [&](const char* stage, uint64_t timetag)
    {
        if (!debug_saved_condition_ref_drift)
        {
            return;
        }

        bool any_change = false;
        std::fprintf(stderr,
                     "saved_condition_ref_drift: prod=%s stage=%s timetag=%llu",
                     prod_name,
                     stage,
                     static_cast<unsigned long long>(timetag));

        for (size_t i = 0; i < (sizeof(tracked_ref_ids) / sizeof(tracked_ref_ids[0])); ++i)
        {
            const uint64_t after_ref = debug_identifier_refcount(thisAgent,
                                                                 tracked_ref_ids[i].letter,
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

    instantiation* inst = NIL;
    init_instantiation(thisAgent,
                       inst,
                       (prod && prod->name) ? prod->name : thisAgent->symbolManager->soarSymbols.architecture_inst_symbol,
                       prod,
                       NIL,
                       NIL);
    emit_saved_condition_ref_drift("post-init", 0);
    inst->in_ms = in_ms;
    inst->match_goal = match_goal;
    inst->match_goal_level = match_level;
    condition* previous_cond = NIL;
    for (const auto& condition_entry : condition_entries)
    {
        wme* matched_wme = create_saved_condition_wme(thisAgent,
                                                      condition_entry.wme(),
                                                      symbol_map,
                                                      preference_map,
                                                      preference_clone_map,
                                                      false);
        if (!matched_wme)
        {
            deallocate_instantiation(thisAgent, inst);
            return NIL;
        }

        if (debug_skip_detached_saved_condition_create && !wme_is_in_rete(thisAgent, matched_wme))
        {
            std::fprintf(stderr,
                         "create_saved_condition_instantiation_skip_detached_wme: %s timetag=%llu in_ms=%d\n",
                         prod_name,
                         static_cast<unsigned long long>(condition_entry.wme().timetag()),
                         in_ms ? 1 : 0);
            continue;
        }

        emit_saved_condition_ref_drift("pre-make-condition", condition_entry.wme().timetag());

        const uint64_t id_ref_before = debug_symbol_refcount_if_identifier(matched_wme->id);
        const uint64_t attr_ref_before = debug_symbol_refcount_if_identifier(matched_wme->attr);
        const uint64_t value_ref_before = debug_symbol_refcount_if_identifier(matched_wme->value);

        condition* cond = make_condition(thisAgent,
                                         make_test(thisAgent, matched_wme->id, EQUALITY_TEST),
                                         make_test(thisAgent, matched_wme->attr, EQUALITY_TEST),
                                         make_test(thisAgent, matched_wme->value, EQUALITY_TEST));

        if (debug_saved_condition_ref_drift)
        {
            debug_emit_symbol_ref_delta_if_identifier(prod_name,
                                                      "post-make-condition",
                                                      condition_entry.wme().timetag(),
                                                      "id",
                                                      matched_wme->id,
                                                      id_ref_before);
            debug_emit_symbol_ref_delta_if_identifier(prod_name,
                                                      "post-make-condition",
                                                      condition_entry.wme().timetag(),
                                                      "attr",
                                                      matched_wme->attr,
                                                      attr_ref_before);
            debug_emit_symbol_ref_delta_if_identifier(prod_name,
                                                      "post-make-condition",
                                                      condition_entry.wme().timetag(),
                                                      "value",
                                                      matched_wme->value,
                                                      value_ref_before);
        }

        emit_saved_condition_ref_drift("post-make-condition", condition_entry.wme().timetag());
        cond->inst = inst;
        cond->bt.wme_ = matched_wme;
        cond->bt.level = matched_wme->id->id->level;
        cond->bt.trace = matched_wme->preference;
        if (condition_entry.has_trace_preference())
        {
            Symbol* trace_id = symbol_by_id(symbol_map, condition_entry.trace_preference().id());
            Symbol* trace_attr = symbol_by_id(symbol_map, condition_entry.trace_preference().attr());
            Symbol* trace_value = symbol_by_id(symbol_map, condition_entry.trace_preference().value());
            if (trace_id && trace_attr && trace_value)
            {
                preference* trace_pref = get_or_create_restored_preference(thisAgent,
                                                                           condition_entry.trace_preference(),
                                                                           trace_id,
                                                                           trace_attr,
                                                                           trace_value,
                                                                           symbol_map,
                                                                           preference_map,
                                                                           preference_clone_map);
                if (trace_pref)
                {
                    cond->bt.trace = trace_pref;
                }
            }
        }
        if (cond->bt.trace && (cond->bt.trace->level > inst->match_goal_level))
        {
            cond->bt.trace = find_clone_for_level(cond->bt.trace, inst->match_goal_level);
        }
        cond->data.tests.id_test->eq_test->inst_identity = condition_entry.id_inst_identity();
        cond->data.tests.attr_test->eq_test->inst_identity = condition_entry.attr_inst_identity();
        cond->data.tests.value_test->eq_test->inst_identity = condition_entry.value_inst_identity();
        cond->test_for_acceptable_preference = matched_wme->acceptable;

        cond->prev = previous_cond;
        cond->next = NIL;
        if (previous_cond)
        {
            previous_cond->next = cond;
            inst->bottom_of_instantiated_conditions = cond;
        }
        else
        {
            inst->top_of_instantiated_conditions = cond;
            inst->bottom_of_instantiated_conditions = cond;
        }
        previous_cond = cond;

        emit_saved_condition_ref_drift("post-link-condition", condition_entry.wme().timetag());
    }

    if (!inst->top_of_instantiated_conditions)
    {
        deallocate_instantiation(thisAgent, inst);
        return NIL;
    }

    finalize_instantiation(thisAgent, inst, false, NIL, false);
    emit_saved_condition_ref_drift("post-finalize", 0);

    int condition_index = 0;
    for (condition* cond = inst->top_of_instantiated_conditions; cond != NIL; cond = cond->next, ++condition_index)
    {
        if (cond->type != POSITIVE_CONDITION)
        {
            continue;
        }

        if (condition_index >= condition_entries.size())
        {
            break;
        }

        const auto& condition_entry = condition_entries.Get(condition_index);
        if (condition_entry.id_identity())
        {
            if (Identity* identity = find_goal_identity_by_id(thisAgent, match_goal, condition_entry.id_identity()))
            {
                set_test_identity(thisAgent, cond->data.tests.id_test->eq_test, identity);
            }
        }
        if (condition_entry.attr_identity())
        {
            if (Identity* identity = find_goal_identity_by_id(thisAgent, match_goal, condition_entry.attr_identity()))
            {
                set_test_identity(thisAgent, cond->data.tests.attr_test->eq_test, identity);
            }
        }
        if (condition_entry.value_identity())
        {
            if (Identity* identity = find_goal_identity_by_id(thisAgent, match_goal, condition_entry.value_identity()))
            {
                set_test_identity(thisAgent, cond->data.tests.value_test->eq_test, identity);
            }
        }
    }

    if (inst->prod_name && inst->prod_name->is_string() && !std::strcmp(inst->prod_name->sc->name, "apply-op1"))
    {
        std::fprintf(stderr, "synthetic_finalize_instantiation apply-op1:\n");
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
                         "  cond: %s | %s | %s | trace=%u trace_level=%d | inst_ids=%llu/%llu/%llu | ids=%llu:%llu/%llu:%llu/%llu:%llu\n",
                         id_test.c_str(),
                         attr_test.c_str(),
                         value_test.c_str(),
                         debug_cond->bt.trace ? 1U : 0U,
                         debug_cond->bt.trace ? static_cast<int>(debug_cond->bt.trace->level) : -1,
                         static_cast<unsigned long long>(id_eq && id_eq->inst_identity ? id_eq->inst_identity : 0),
                         static_cast<unsigned long long>(attr_eq && attr_eq->inst_identity ? attr_eq->inst_identity : 0),
                         static_cast<unsigned long long>(value_eq && value_eq->inst_identity ? value_eq->inst_identity : 0),
                         static_cast<unsigned long long>(id_eq && id_eq->identity ? id_eq->identity->get_sub_identity() : 0),
                         static_cast<unsigned long long>(id_eq && id_eq->identity ? id_eq->identity->get_identity() : 0),
                         static_cast<unsigned long long>(attr_eq && attr_eq->identity ? attr_eq->identity->get_sub_identity() : 0),
                         static_cast<unsigned long long>(attr_eq && attr_eq->identity ? attr_eq->identity->get_identity() : 0),
                         static_cast<unsigned long long>(value_eq && value_eq->identity ? value_eq->identity->get_sub_identity() : 0),
                         static_cast<unsigned long long>(value_eq && value_eq->identity ? value_eq->identity->get_identity() : 0));
        }
    }

    return inst;
}

void restore_instantiated_condition_identities(agent* thisAgent,
                                              instantiation* inst,
                                              Symbol* match_goal,
                                              const google::protobuf::RepeatedPtrField<soar::kernel::InstantiatedConditionEntry>& condition_entries)
{
    if (!inst || !match_goal || (condition_entries.size() == 0))
    {
        return;
    }

    int condition_index = 0;
    for (condition* cond = inst->top_of_instantiated_conditions; cond != NIL; cond = cond->next, ++condition_index)
    {
        if (cond->type != POSITIVE_CONDITION)
        {
            continue;
        }

        if (condition_index >= condition_entries.size())
        {
            break;
        }

        const auto& condition_entry = condition_entries.Get(condition_index);
        if (condition_entry.id_identity())
        {
            if (Identity* identity = find_goal_identity_by_id(thisAgent, match_goal, condition_entry.id_identity()))
            {
                set_test_identity(thisAgent, cond->data.tests.id_test->eq_test, identity);
            }
        }
        if (condition_entry.attr_identity())
        {
            if (Identity* identity = find_goal_identity_by_id(thisAgent, match_goal, condition_entry.attr_identity()))
            {
                set_test_identity(thisAgent, cond->data.tests.attr_test->eq_test, identity);
            }
        }
        if (condition_entry.value_identity())
        {
            if (Identity* identity = find_goal_identity_by_id(thisAgent, match_goal, condition_entry.value_identity()))
            {
                set_test_identity(thisAgent, cond->data.tests.value_test->eq_test, identity);
            }
        }
    }
}

instantiation* create_saved_condition_instantiation(agent* thisAgent,
                                                    production* prod,
                                                    Symbol* match_goal,
                                                    goal_stack_level match_level,
                                                    const google::protobuf::RepeatedPtrField<soar::kernel::WmeEntry>& condition_wmes,
                                                    const SymbolReverseMap& symbol_map,
                                                    RestoredPreferenceMap& preference_map,
                                                    RestoredPreferenceCloneMap& preference_clone_map,
                                                    bool in_ms)
{
    if (!prod || !match_goal || (condition_wmes.size() == 0))
    {
        return NIL;
    }

    instantiation* inst = NIL;
    init_instantiation(thisAgent,
                       inst,
                       (prod && prod->name) ? prod->name : thisAgent->symbolManager->soarSymbols.architecture_inst_symbol,
                       NIL,
                       NIL,
                       NIL);
    inst->in_ms = in_ms;
    inst->match_goal = match_goal;
    inst->match_goal_level = match_level;
    condition* previous_cond = NIL;
    for (const auto& wme_entry : condition_wmes)
    {
        wme* matched_wme = create_saved_condition_wme(thisAgent,
                                                      wme_entry,
                                                      symbol_map,
                                                      preference_map,
                                                      preference_clone_map);
        if (!matched_wme)
        {
            deallocate_instantiation(thisAgent, inst);
            return NIL;
        }

        condition* cond = make_condition(thisAgent,
                                         make_test(thisAgent, matched_wme->id, EQUALITY_TEST),
                                         make_test(thisAgent, matched_wme->attr, EQUALITY_TEST),
                                         make_test(thisAgent, matched_wme->value, EQUALITY_TEST));
        cond->inst = inst;
        cond->bt.wme_ = matched_wme;
        cond->bt.level = matched_wme->id->id->level;
        cond->bt.trace = matched_wme->preference;
        if (cond->bt.trace && (cond->bt.trace->level > inst->match_goal_level))
        {
            cond->bt.trace = find_clone_for_level(cond->bt.trace, inst->match_goal_level);
        }
        cond->test_for_acceptable_preference = matched_wme->acceptable;

        cond->prev = previous_cond;
        cond->next = NIL;
        if (previous_cond)
        {
            previous_cond->next = cond;
            inst->bottom_of_instantiated_conditions = cond;
        }
        else
        {
            inst->top_of_instantiated_conditions = cond;
            inst->bottom_of_instantiated_conditions = cond;
        }
        previous_cond = cond;
    }

    return inst;
}