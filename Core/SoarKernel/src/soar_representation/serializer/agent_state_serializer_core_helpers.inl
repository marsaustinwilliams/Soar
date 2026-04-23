/* Preserve refs to common int constants (0-100) before reteload cleanup removes them.
   This prevents double-free issues when releases_common_variables_and_numbers()
   tries to remove symbols that were already freed by reteload_free_symbol_table(). */
inline void preserve_common_number_symbols(agent* thisAgent, std::vector<Symbol*>& preserved_symbols)
{
    preserved_symbols.clear();
    for (int i = 0; i <= 100; ++i)
    {
        Symbol* sym = thisAgent->symbolManager->find_int_constant(i);
        if (sym)
        {
            thisAgent->symbolManager->symbol_add_ref(sym);
            preserved_symbols.push_back(sym);
        }
    }
}

inline void preserve_symbol_if_present(agent* thisAgent,
                                       Symbol* sym,
                                       std::vector<Symbol*>& preserved_symbols)
{
    if (!thisAgent || !sym)
    {
        return;
    }

    thisAgent->symbolManager->symbol_add_ref(sym);
    preserved_symbols.push_back(sym);
}

/* Preserve predefined symbols that are heavily involved in bootstrap state
   teardown immediately after rete import. Holding temporary refs here prevents
   transient teardown paths from deallocating these interned symbols while
   there are still buffered structures that may reference them. */
inline void preserve_restore_critical_predefined_symbols(agent* thisAgent,
                                                         std::vector<Symbol*>& preserved_symbols)
{
    if (!thisAgent)
    {
        return;
    }

    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.state_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.operator_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.superstate_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.problem_space_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.io_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.object_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.attribute_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.impasse_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.choices_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.none_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.constraint_failure_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.no_change_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.multiple_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.conflict_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.tie_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.item_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.item_count_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.non_numeric_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.non_numeric_count_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.constant_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.quiescence_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.t_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.nil_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.type_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.goal_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.name_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.input_link_symbol, preserved_symbols);
    preserve_symbol_if_present(thisAgent, thisAgent->symbolManager->soarSymbols.output_link_symbol, preserved_symbols);
}

/* Some restore flows execute command-level reset/excise before entering
   restore_agent_state_message(), which can leave selected predefined symbol
   pointers dangling (while still cached in soarSymbols). Rebind the symbols
   by interned name so subsequent teardown paths operate on live symbols. */
inline void rebind_restore_critical_predefined_symbols(agent* thisAgent)
{
    if (!thisAgent)
    {
        return;
    }

    auto rebind_symbol = [&](Symbol*& target, const char* name)
    {
        Symbol* rebound = thisAgent->symbolManager->find_str_constant(name);
        if (!rebound)
        {
            rebound = thisAgent->symbolManager->make_str_constant(name);
        }
        target = rebound;
    };

    auto& syms = thisAgent->symbolManager->soarSymbols;
    rebind_symbol(syms.state_symbol, "state");
    rebind_symbol(syms.operator_symbol, "operator");
    rebind_symbol(syms.superstate_symbol, "superstate");
    rebind_symbol(syms.problem_space_symbol, "problem-space");
    rebind_symbol(syms.io_symbol, "io");
    rebind_symbol(syms.object_symbol, "object");
    rebind_symbol(syms.attribute_symbol, "attribute");
    rebind_symbol(syms.impasse_symbol, "impasse");
    rebind_symbol(syms.choices_symbol, "choices");
    rebind_symbol(syms.none_symbol, "none");
    rebind_symbol(syms.constraint_failure_symbol, "constraint-failure");
    rebind_symbol(syms.no_change_symbol, "no-change");
    rebind_symbol(syms.multiple_symbol, "multiple");
    rebind_symbol(syms.conflict_symbol, "conflict");
    rebind_symbol(syms.tie_symbol, "tie");
    rebind_symbol(syms.item_symbol, "item");
    rebind_symbol(syms.item_count_symbol, "item-count");
    rebind_symbol(syms.non_numeric_symbol, "non-numeric");
    rebind_symbol(syms.non_numeric_count_symbol, "non-numeric-count");
    rebind_symbol(syms.constant_symbol, "constant");
    rebind_symbol(syms.quiescence_symbol, "quiescence");
    rebind_symbol(syms.t_symbol, "t");
    rebind_symbol(syms.nil_symbol, "nil");
    rebind_symbol(syms.type_symbol, "type");
    rebind_symbol(syms.goal_symbol, "goal");
    rebind_symbol(syms.name_symbol, "name");
    rebind_symbol(syms.input_link_symbol, "input-link");
    rebind_symbol(syms.output_link_symbol, "output-link");
}

inline bool symbol_is_live_in_tables(agent* thisAgent, Symbol* sym)
{
    if (!thisAgent || !sym)
    {
        return true;
    }

    switch (sym->symbol_type)
    {
        case STR_CONSTANT_SYMBOL_TYPE:
            return thisAgent->symbolManager->find_str_constant(sym->sc->name) == sym;

        case INT_CONSTANT_SYMBOL_TYPE:
            return thisAgent->symbolManager->find_int_constant(sym->ic->value) == sym;

        case FLOAT_CONSTANT_SYMBOL_TYPE:
            return thisAgent->symbolManager->find_float_constant(sym->fc->value) == sym;

        case VARIABLE_SYMBOL_TYPE:
            return thisAgent->symbolManager->find_variable(sym->var->name) == sym;

        case IDENTIFIER_SYMBOL_TYPE:
            return thisAgent->symbolManager->find_identifier(sym->id->name_letter, sym->id->name_number) == sym;

        default:
            return true;
    }
}

inline bool wme_symbols_are_live(agent* thisAgent,
                                 wme* candidate,
                                 const char* list_name,
                                 std::string& reason)
{
    if (!candidate)
    {
        return true;
    }

    if (!symbol_is_live_in_tables(thisAgent, candidate->id))
    {
        reason = std::string("Dangling WME id symbol in ") + list_name;
        return false;
    }

    if (!symbol_is_live_in_tables(thisAgent, candidate->attr))
    {
        reason = std::string("Dangling WME attr symbol in ") + list_name;
        return false;
    }

    if (!symbol_is_live_in_tables(thisAgent, candidate->value))
    {
        reason = std::string("Dangling WME value symbol in ") + list_name;
        return false;
    }

    if (candidate->preference)
    {
        preference* pref = candidate->preference;
        if (!symbol_is_live_in_tables(thisAgent, pref->id) ||
            !symbol_is_live_in_tables(thisAgent, pref->attr) ||
            !symbol_is_live_in_tables(thisAgent, pref->value) ||
            (pref->referent && !symbol_is_live_in_tables(thisAgent, pref->referent)))
        {
            reason = std::string("Dangling preference symbol referenced by WME in ") + list_name;
            return false;
        }
    }

    return true;
}

inline bool validate_runtime_symbol_liveness_for_save(agent* thisAgent,
                                                      std::string& reason)
{
    if (!thisAgent)
    {
        return true;
    }

    for (wme* current = thisAgent->all_wmes_in_rete; current != NIL; current = current->rete_next)
    {
        if (!wme_symbols_are_live(thisAgent, current, "all_wmes_in_rete", reason))
        {
            return false;
        }
    }

    for (cons* current = thisAgent->wmes_to_add; current != NIL; current = current->rest)
    {
        if (!wme_symbols_are_live(thisAgent, static_cast<wme*>(current->first), "wmes_to_add", reason))
        {
            return false;
        }
    }

    for (cons* current = thisAgent->wmes_to_remove; current != NIL; current = current->rest)
    {
        if (!wme_symbols_are_live(thisAgent, static_cast<wme*>(current->first), "wmes_to_remove", reason))
        {
            return false;
        }
    }

    auto& syms = thisAgent->symbolManager->soarSymbols;
    struct NamedCriticalSymbol
    {
        const char* name;
        Symbol* sym;
    };

    NamedCriticalSymbol critical[] = {
        {"state", syms.state_symbol},
        {"operator", syms.operator_symbol},
        {"superstate", syms.superstate_symbol},
        {"problem-space", syms.problem_space_symbol},
        {"io", syms.io_symbol},
        {"object", syms.object_symbol},
        {"attribute", syms.attribute_symbol},
        {"impasse", syms.impasse_symbol},
        {"choices", syms.choices_symbol},
        {"none", syms.none_symbol},
        {"constraint-failure", syms.constraint_failure_symbol},
        {"no-change", syms.no_change_symbol},
        {"multiple", syms.multiple_symbol},
        {"conflict", syms.conflict_symbol},
        {"tie", syms.tie_symbol},
        {"item", syms.item_symbol},
        {"item-count", syms.item_count_symbol},
        {"non-numeric", syms.non_numeric_symbol},
        {"non-numeric-count", syms.non_numeric_count_symbol},
        {"constant", syms.constant_symbol},
        {"quiescence", syms.quiescence_symbol},
        {"t", syms.t_symbol},
        {"nil", syms.nil_symbol},
        {"type", syms.type_symbol},
        {"goal", syms.goal_symbol},
        {"name", syms.name_symbol},
        {"input-link", syms.input_link_symbol},
        {"output-link", syms.output_link_symbol}
    };

    for (const NamedCriticalSymbol& entry : critical)
    {
        if (!symbol_is_live_in_tables(thisAgent, entry.sym))
        {
            reason = std::string("Dangling critical predefined symbol: ") + entry.name;
            return false;
        }
    }

    return true;
}

inline bool restore_preference_validation_enabled()
{
    const char* env = std::getenv("SOAR_RESTORE_VALIDATE_PREFS");
    if (!env || !env[0])
    {
        return false;
    }

    if ((env[0] == '0') || !std::strcmp(env, "false") || !std::strcmp(env, "off"))
    {
        return false;
    }

    return true;
}

inline bool validate_preference_integrity(agent* thisAgent,
                                          preference* pref,
                                          const char* source,
                                          std::string& reason)
{
    if (!pref)
    {
        return true;
    }

    if (!pref->id || !pref->attr || !pref->value)
    {
        reason = std::string(source) + ": preference has null id/attr/value";
        return false;
    }

    const int pref_type = static_cast<int>(pref->type);
    if ((pref_type < 0) || (pref_type >= static_cast<int>(NUM_PREFERENCE_TYPES)))
    {
        reason = std::string(source) + ": preference has invalid type";
        return false;
    }

    if (pref->in_tm && !pref->slot)
    {
        reason = std::string(source) + ": in_tm preference has null slot";
        return false;
    }

    if (!pref->in_tm && pref->slot)
    {
        reason = std::string(source) + ": non-TM preference has non-null slot";
        return false;
    }

    if (pref->in_tm && !pref->inst)
    {
        reason = std::string(source) + ": in_tm preference has null instantiation";
        return false;
    }

    if (pref->in_tm)
    {
        bool in_slot_all_preferences = false;
        for (preference* iter = pref->slot->all_preferences; iter != NIL; iter = iter->all_of_slot_next)
        {
            if (iter == pref)
            {
                in_slot_all_preferences = true;
                break;
            }
        }
        if (!in_slot_all_preferences)
        {
            reason = std::string(source) + ": in_tm preference missing from slot all_preferences";
            return false;
        }

        bool in_slot_type_preferences = false;
        for (preference* iter = pref->slot->preferences[pref_type]; iter != NIL; iter = iter->next)
        {
            if (iter == pref)
            {
                in_slot_type_preferences = true;
                break;
            }
        }
        if (!in_slot_type_preferences)
        {
            reason = std::string(source) + ": in_tm preference missing from slot typed preference list";
            return false;
        }
    }

    return true;
}

inline bool validate_restore_preference_integrity(agent* thisAgent,
                                                  std::string& reason)
{
    if (!thisAgent)
    {
        return true;
    }

    for (wme* current = thisAgent->all_wmes_in_rete; current != NIL; current = current->rete_next)
    {
        if (!validate_preference_integrity(thisAgent, current->preference, "all_wmes_in_rete", reason))
        {
            return false;
        }
    }

    for (Symbol* goal = thisAgent->top_goal; goal != NIL; goal = goal->id->lower_goal)
    {
        for (wme* current = goal->id->impasse_wmes; current != NIL; current = current->next)
        {
            if (!validate_preference_integrity(thisAgent, current->preference, "impasse_wmes", reason))
            {
                return false;
            }
        }
    }

    return true;
}

enum class StagedValidationMode
{
    Off,
    Sampled,
    Strict
};

inline StagedValidationMode get_staged_validation_mode()
{
    const char* mode_env = std::getenv("SOAR_SERIALIZER_STAGED_VALIDATION");
    if (!mode_env || !mode_env[0])
    {
        return StagedValidationMode::Off;
    }

    if (!std::strcmp(mode_env, "strict"))
    {
        return StagedValidationMode::Strict;
    }

    if (!std::strcmp(mode_env, "sample") || !std::strcmp(mode_env, "sampled"))
    {
        return StagedValidationMode::Sampled;
    }

    if (!std::strcmp(mode_env, "off"))
    {
        return StagedValidationMode::Off;
    }

    return StagedValidationMode::Off;
}

inline uint64_t get_staged_validation_sample_interval()
{
    const char* interval_env = std::getenv("SOAR_SERIALIZER_STAGED_VALIDATION_EVERY");
    if (!interval_env || !interval_env[0])
    {
        return 10;
    }

    char* parse_end = nullptr;
    unsigned long parsed = std::strtoul(interval_env, &parse_end, 10);
    if ((parse_end == interval_env) || (parsed == 0))
    {
        return 10;
    }

    return static_cast<uint64_t>(parsed);
}

inline bool compute_staged_validation_for_next_save_cycle()
{
    static std::atomic<uint64_t> save_cycle_counter{0};
    const StagedValidationMode mode = get_staged_validation_mode();

    if (mode == StagedValidationMode::Strict)
    {
        ++save_cycle_counter;
        return true;
    }

    if (mode == StagedValidationMode::Off)
    {
        ++save_cycle_counter;
        return false;
    }

    const uint64_t cycle_index = ++save_cycle_counter;
    const uint64_t sample_every = get_staged_validation_sample_interval();
    return (cycle_index == 1) || ((cycle_index % sample_every) == 0);
}

static thread_local bool g_current_save_cycle_staged_validation_enabled = false;

inline void set_current_save_cycle_staged_validation_enabled(bool enabled)
{
    g_current_save_cycle_staged_validation_enabled = enabled;
}

inline bool current_save_cycle_staged_validation_enabled()
{
    return g_current_save_cycle_staged_validation_enabled;
}

inline void release_preserved_common_number_symbols(agent* thisAgent, std::vector<Symbol*>& preserved_symbols)
{
    for (Symbol* sym : preserved_symbols)
    {
        thisAgent->symbolManager->symbol_remove_ref(&sym);
    }
    preserved_symbols.clear();
}

/* Keep this in sync with Symbol_Manager::create_common_variables_and_numbers()
   and Symbol_Manager::release_common_variables_and_numbers(), which currently
   preallocate/release integer constants in the inclusive range [0, 100].
   This is unrelated to identifier name numbers, which may be much larger.
   Use make_int_constant() unconditionally here so we re-establish the same
   persistent ref that create_common_variables_and_numbers() maintains. */
inline void ensure_common_int_constants_exist_0_to_100(agent* thisAgent)
{
    for (int i = 0; i <= 100; ++i)
    {
        thisAgent->symbolManager->make_int_constant(i);
    }
}

wme* find_wme_in_owner_list(wme* head, Symbol* id, Symbol* attr)
{
    for (wme* current = head; current != NIL; current = current->next)
    {
        if ((current->id == id) && (current->attr == attr))
        {
            return current;
        }
    }

    return NIL;
}

bool serializer_should_manage_cond_trace_refs(goal_stack_level match_goal_level)
{
#ifndef DO_TOP_LEVEL_COND_REF_CTS
    return (match_goal_level > TOP_GOAL_LEVEL);
#else
    return true;
#endif
}

bool wme_is_in_list(wme* head, wme* target)
{
    for (wme* current = head; current != NIL; current = current->next)
    {
        if (current == target)
        {
            return true;
        }
    }

    return false;
}

wme* find_wme_with_id_attr(agent* thisAgent, Symbol* id, Symbol* attr)
{
    if (id && attr)
    {
        if (slot* s = find_slot(id, attr))
        {
            if (wme* slot_wme = find_wme_in_owner_list(s->wmes, id, attr))
            {
                return slot_wme;
            }

            if (wme* acceptable_wme = find_wme_in_owner_list(s->acceptable_preference_wmes, id, attr))
            {
                return acceptable_wme;
            }
        }

        if (id->is_sti())
        {
            if (wme* impasse_wme = find_wme_in_owner_list(id->id->impasse_wmes, id, attr))
            {
                return impasse_wme;
            }

            if (wme* input_wme = find_wme_in_owner_list(id->id->input_wmes, id, attr))
            {
                return input_wme;
            }
        }
    }

    for (wme* current = thisAgent->all_wmes_in_rete; current != NIL; current = current->rete_next)
    {
        if (current->id == id && current->attr == attr)
        {
            return current;
        }
    }

    return NIL;
}

uint64_t count_live_rete_wmes(agent* thisAgent)
{
    uint64_t count = 0;
    for (wme* current = thisAgent->all_wmes_in_rete; current != NIL; current = current->rete_next)
    {
        ++count;
    }

    return count;
}

uint64_t count_wme_cons_list(cons* head)
{
    uint64_t count = 0;
    for (cons* current = head; current != NIL; current = current->rest)
    {
        ++count;
    }

    return count;
}

uint64_t count_ms_change_list(ms_change* head)
{
    uint64_t count = 0;
    for (ms_change* current = head; current != NIL; current = current->next)
    {
        ++count;
    }

    return count;
}

bool wme_is_in_rete(agent* thisAgent, wme* target)
{
    for (wme* current = thisAgent->all_wmes_in_rete; current != NIL; current = current->rete_next)
    {
        if (current == target)
        {
            return true;
        }
    }

    return false;
}

bool equivalent_wme_exists_in_rete(agent* thisAgent, wme* target)
{
    for (wme* current = thisAgent->all_wmes_in_rete; current != NIL; current = current->rete_next)
    {
        if ((current->id == target->id) &&
            (current->attr == target->attr) &&
            (current->value == target->value) &&
            (current->acceptable == target->acceptable) &&
            (current->timetag == target->timetag))
        {
            return true;
        }
    }

    return false;
}

void add_internal_wme_if_detached(agent* thisAgent,
                                  wme* candidate,
                                  std::unordered_set<wme*>& detached_wmes)
{
    if (candidate && !wme_is_in_rete(thisAgent, candidate))
    {
        detached_wmes.insert(candidate);
    }
}

uint64_t count_detached_internal_wmes(agent* thisAgent)
{
    std::unordered_set<wme*> detached_wmes;

    add_internal_wme_if_detached(thisAgent, thisAgent->io_header_link, detached_wmes);

    for (output_link* output = thisAgent->existing_output_links; output != NIL; output = output->next)
    {
        add_internal_wme_if_detached(thisAgent, output->link_wme, detached_wmes);
    }

    for (Symbol* goal = thisAgent->top_goal; goal != NIL; goal = goal->id->lower_goal)
    {
        if (!goal->is_sti())
        {
            continue;
        }

        if (goal->id->rl_info)
        {
            add_internal_wme_if_detached(thisAgent, goal->id->rl_info->rl_link_wme, detached_wmes);
        }

        if (goal->id->epmem_info)
        {
            add_internal_wme_if_detached(thisAgent, goal->id->epmem_info->epmem_link_wme, detached_wmes);
            add_internal_wme_if_detached(thisAgent, goal->id->epmem_info->cmd_wme, detached_wmes);
            add_internal_wme_if_detached(thisAgent, goal->id->epmem_info->result_wme, detached_wmes);
            add_internal_wme_if_detached(thisAgent, goal->id->epmem_info->epmem_time_wme, detached_wmes);
        }

        if (goal->id->smem_info)
        {
            add_internal_wme_if_detached(thisAgent, goal->id->smem_info->smem_link_wme, detached_wmes);
            add_internal_wme_if_detached(thisAgent, goal->id->smem_info->cmd_wme, detached_wmes);
            add_internal_wme_if_detached(thisAgent, goal->id->smem_info->result_wme, detached_wmes);
        }
    }

    return static_cast<uint64_t>(detached_wmes.size());
}

struct SyntheticPreferenceLifecycle
{
    std::unordered_set<preference*> restore_owned;
    std::unordered_set<preference*> init_owned;

    void track_restore_owned(preference* pref)
    {
        if (pref)
        {
            restore_owned.insert(pref);
        }
    }

    void track_init_owned(preference* pref)
    {
        if (pref)
        {
            init_owned.insert(pref);
        }
    }
};

RestoredPreferenceKey make_restored_preference_key(::PreferenceType pref_type,
                                                   bool o_supported,
                                                   bool in_tm,
                                                   goal_stack_level match_goal_level,
                                                   Symbol* id,
                                                   Symbol* attr,
                                                   Symbol* value,
                                                   Symbol* referent)
{
    return std::to_string(static_cast<int>(pref_type)) + ":" +
           std::to_string(o_supported ? 1 : 0) + ":" +
           std::to_string(in_tm ? 1 : 0) + ":" +
           std::to_string(static_cast<int64_t>(match_goal_level)) + ":" +
           std::to_string(reinterpret_cast<uintptr_t>(id)) + ":" +
           std::to_string(reinterpret_cast<uintptr_t>(attr)) + ":" +
           std::to_string(reinterpret_cast<uintptr_t>(value)) + ":" +
           std::to_string(reinterpret_cast<uintptr_t>(referent));
}

RestoredPreferenceCloneKey make_restored_preference_clone_key(::PreferenceType pref_type,
                                                              bool o_supported,
                                                              bool in_tm,
                                                              Symbol* id,
                                                              Symbol* attr,
                                                              Symbol* value,
                                                              Symbol* referent)
{
    return std::to_string(static_cast<int>(pref_type)) + ":" +
           std::to_string(o_supported ? 1 : 0) + ":" +
           std::to_string(in_tm ? 1 : 0) + ":" +
           std::to_string(reinterpret_cast<uintptr_t>(id)) + ":" +
           std::to_string(reinterpret_cast<uintptr_t>(attr)) + ":" +
           std::to_string(reinterpret_cast<uintptr_t>(value)) + ":" +
           std::to_string(reinterpret_cast<uintptr_t>(referent));
}

void link_restored_preference_clone(preference* pref,
                                    const RestoredPreferenceCloneKey& clone_key,
                                    RestoredPreferenceCloneMap& preference_clone_map)
{
    auto existing = preference_clone_map.find(clone_key);
    if (existing == preference_clone_map.end())
    {
        preference_clone_map.emplace(clone_key, pref);
        return;
    }

    preference* head = existing->second;
    if ((head == NIL) || (head == pref))
    {
        existing->second = pref;
        return;
    }

    pref->next_clone = head;
    pref->prev_clone = NIL;
    head->prev_clone = pref;
    existing->second = pref;
}

RestoredAssertionKey make_restored_assertion_key(agent* /*thisAgent*/, ms_change* assertion)
{
    return std::to_string(reinterpret_cast<uintptr_t>(assertion ? assertion->p_node : NIL)) + ":" +
           std::to_string(reinterpret_cast<uintptr_t>(assertion ? assertion->tok : NIL)) + ":" +
           std::to_string(reinterpret_cast<uintptr_t>(assertion ? assertion->w : NIL)) + ":" +
           std::to_string(reinterpret_cast<uintptr_t>(assertion ? assertion->inst : NIL)) + ":" +
           std::to_string(reinterpret_cast<uintptr_t>(assertion ? assertion->goal : NIL)) + ":" +
           std::to_string(static_cast<int64_t>(assertion ? assertion->level : 0));
}

bool preference_type_is_valid(int pref_type)
{
    return (pref_type >= static_cast<int>(ACCEPTABLE_PREFERENCE_TYPE)) &&
           (pref_type < static_cast<int>(NUM_PREFERENCE_TYPES));
}