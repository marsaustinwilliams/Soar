namespace
{

struct SerializerStateSignature
{
    uint64_t identifier_count = 0;
    uint64_t goal_count = 0;
    uint64_t operator_count = 0;
    uint64_t impasse_count = 0;
    uint64_t ids_with_slots = 0;
    uint64_t slot_count = 0;
    uint64_t slot_wmes = 0;
    uint64_t slot_acceptable_wmes = 0;
    uint64_t slot_preferences = 0;
    uint64_t ids_with_input_wmes = 0;
    uint64_t input_wmes = 0;
    uint64_t ids_with_impasse_wmes = 0;
    uint64_t impasse_wmes = 0;
    uint64_t ids_with_operator_slot = 0;
    uint64_t ids_with_assoc_ol = 0;
    uint64_t ids_with_rl = 0;
    uint64_t ids_with_epmem = 0;
    uint64_t ids_with_smem = 0;
    uint64_t rete_wmes = 0;
    uint64_t match_set_productions = 0;
    uint64_t match_set_instantiations = 0;
    uint64_t ms_o_assertions = 0;
    uint64_t ms_i_assertions = 0;
    uint64_t ms_retractions = 0;
    uint64_t postponed_assertions = 0;
    uint64_t nil_goal_retractions = 0;
    uint64_t output_links = 0;
    uint64_t hash = 1469598103934665603ULL;
};

struct SerializerDetailedSignature
{
    struct IdFieldState
    {
        uint64_t reference_count = 0;
        uint64_t level = 0;
        bool isa_goal = false;
        bool isa_operator = false;
        bool isa_impasse = false;
        bool has_operator_slot = false;
        bool has_higher_goal = false;
        bool has_lower_goal = false;
        bool has_rl = false;
        bool has_epmem = false;
        bool has_smem = false;
        bool has_gds = false;
        uint64_t link_count = 0;
    };

    std::vector<uint64_t> identifier_keys;
    std::vector<std::pair<std::string, uint64_t>> match_instantiations_by_production;
    std::vector<std::pair<uint64_t, uint64_t>> identifier_field_hashes;
    std::vector<std::pair<uint64_t, IdFieldState>> identifier_field_states;
    std::vector<std::string> rete_wme_signatures;
    std::vector<std::pair<uint64_t, std::string>> identifier_wme_contexts;
};

struct SavedSerializerSignature
{
    bool valid = false;
    std::string agent_name;
    std::string source_name;
    SerializerStateSignature signature;
    SerializerDetailedSignature detail;
};

SavedSerializerSignature g_saved_serializer_signature;
std::unordered_map<agent*, char*> g_restored_reason_for_stopping_storage;

void set_restored_reason_for_stopping(agent* thisAgent, const std::string& reason)
{
    auto existing = g_restored_reason_for_stopping_storage.find(thisAgent);
    if (existing != g_restored_reason_for_stopping_storage.end() && existing->second)
    {
        free_memory_block_for_string(thisAgent, existing->second);
        existing->second = NIL;
    }

    if (reason.empty())
    {
        thisAgent->reason_for_stopping = "";
        return;
    }

    char* restored_reason = make_memory_block_for_string(thisAgent, reason.c_str());
    g_restored_reason_for_stopping_storage[thisAgent] = restored_reason;
    thisAgent->reason_for_stopping = restored_reason;
}

bool serializer_signature_enabled()
{
    return (std::getenv("SOAR_SERIALIZER_SIGNATURE_CHECK") != nullptr);
}

bool serializer_signature_strict_enabled()
{
    return (std::getenv("SOAR_SERIALIZER_SIGNATURE_STRICT") != nullptr);
}

void mix_signature(uint64_t& hash, uint64_t value)
{
    hash ^= value;
    hash *= 1099511628211ULL;
}

uint64_t count_wme_list(wme* head)
{
    uint64_t count = 0;
    for (wme* current = head; current != NIL; current = current->next)
    {
        ++count;
    }
    return count;
}

uint64_t count_preference_list(preference* head)
{
    uint64_t count = 0;
    for (preference* current = head; current != NIL; current = current->all_of_slot_next)
    {
        ++count;
    }
    return count;
}

uint64_t count_cons_list(cons* head)
{
    uint64_t count = 0;
    for (cons* current = head; current != NIL; current = current->rest)
    {
        ++count;
    }
    return count;
}

uint64_t count_ms_level_list(ms_change* head)
{
    uint64_t count = 0;
    for (ms_change* current = head; current != NIL; current = current->next_in_level)
    {
        ++count;
    }
    return count;
}

uint64_t count_match_set_instantiations(agent* thisAgent, uint64_t& production_count)
{
    uint64_t total_instantiations = 0;
    production_count = 0;

    for (int type = 0; type < NUM_PRODUCTION_TYPES; ++type)
    {
        for (production* prod = thisAgent->all_productions_of_type[type]; prod != NIL; prod = prod->next)
        {
            if (!prod->instantiations)
            {
                continue;
            }

            ++production_count;
            for (instantiation* inst = prod->instantiations; inst != NIL; inst = inst->next)
            {
                ++total_instantiations;
            }
        }
    }

    return total_instantiations;
}

uint64_t encode_identifier_key(char letter, uint64_t number)
{
    return (static_cast<uint64_t>(static_cast<unsigned char>(letter)) << 56) |
           (number & 0x00FFFFFFFFFFFFFFULL);
}

bool reset_rete_alpha_mem_retesave_index(agent* /*thisAgent*/, void* item, void* /*userdata*/)
{
    alpha_mem* am = static_cast<alpha_mem*>(item);
    if (am)
    {
        am->retesave_amindex = 0;
    }
    return false;
}

std::string format_identifier_key(uint64_t key)
{
    const char letter = static_cast<char>((key >> 56) & 0xFF);
    const uint64_t number = key & 0x00FFFFFFFFFFFFFFULL;
    return std::string(1, letter) + std::to_string(number);
}

std::string production_name_or_placeholder(production* prod)
{
    if (!prod || !prod->name)
    {
        return "<unnamed>";
    }

    const char* name = prod->name->to_string(true);
    return name ? std::string(name) : std::string("<unnamed>");
}

std::string symbol_debug_name(Symbol* sym)
{
    if (!sym)
    {
        return "<nil>";
    }

    if (sym->is_sti())
    {
        return std::string(1, sym->id->name_letter) + std::to_string(sym->id->name_number);
    }

    const char* rendered = sym->to_string(true);
    return rendered ? std::string(rendered) : std::string("<sym>");
}

void append_identifier_wme_context(SerializerDetailedSignature& detail,
                                   uint64_t key,
                                   const std::string& context)
{
    detail.identifier_wme_contexts.emplace_back(key, context);
}

SerializerDetailedSignature build_serializer_detailed_signature(agent* thisAgent)
{
    SerializerDetailedSignature detail;

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

            const uint64_t key = encode_identifier_key(letter, number);
            detail.identifier_keys.push_back(key);

            uint64_t id_hash = 1469598103934665603ULL;
            mix_signature(id_hash, static_cast<uint64_t>(sym->id->name_letter));
            mix_signature(id_hash, sym->id->name_number);
            mix_signature(id_hash, static_cast<uint64_t>(sym->reference_count));
            mix_signature(id_hash, static_cast<uint64_t>(sym->id->level));
            mix_signature(id_hash, sym->id->isa_goal ? 1 : 0);
            mix_signature(id_hash, sym->id->isa_operator ? 1 : 0);
            mix_signature(id_hash, sym->id->isa_impasse ? 1 : 0);
            mix_signature(id_hash, sym->id->operator_slot ? 1 : 0);
            mix_signature(id_hash, sym->id->higher_goal ? 1 : 0);
            mix_signature(id_hash, sym->id->lower_goal ? 1 : 0);
            mix_signature(id_hash, sym->id->rl_info ? 1 : 0);
            mix_signature(id_hash, sym->id->epmem_info ? 1 : 0);
            mix_signature(id_hash, sym->id->smem_info ? 1 : 0);
            mix_signature(id_hash, sym->id->gds ? 1 : 0);
            mix_signature(id_hash, static_cast<uint64_t>(sym->id->link_count));
            detail.identifier_field_hashes.emplace_back(key, id_hash);

            SerializerDetailedSignature::IdFieldState field_state;
            field_state.reference_count = static_cast<uint64_t>(sym->reference_count);
            field_state.level = static_cast<uint64_t>(sym->id->level);
            field_state.isa_goal = (sym->id->isa_goal != 0);
            field_state.isa_operator = (sym->id->isa_operator != 0);
            field_state.isa_impasse = (sym->id->isa_impasse != 0);
            field_state.has_operator_slot = (sym->id->operator_slot != NIL);
            field_state.has_higher_goal = (sym->id->higher_goal != NIL);
            field_state.has_lower_goal = (sym->id->lower_goal != NIL);
            field_state.has_rl = (sym->id->rl_info != NIL);
            field_state.has_epmem = (sym->id->epmem_info != NIL);
            field_state.has_smem = (sym->id->smem_info != NIL);
            field_state.has_gds = (sym->id->gds != NIL);
            field_state.link_count = static_cast<uint64_t>(sym->id->link_count);
            detail.identifier_field_states.emplace_back(key, field_state);
        }
    }

    std::sort(detail.identifier_keys.begin(), detail.identifier_keys.end());
    std::sort(detail.identifier_field_hashes.begin(), detail.identifier_field_hashes.end(),
              [](const std::pair<uint64_t, uint64_t>& lhs, const std::pair<uint64_t, uint64_t>& rhs)
              {
                  return lhs.first < rhs.first;
              });
    std::sort(detail.identifier_field_states.begin(), detail.identifier_field_states.end(),
              [](const std::pair<uint64_t, SerializerDetailedSignature::IdFieldState>& lhs,
                 const std::pair<uint64_t, SerializerDetailedSignature::IdFieldState>& rhs)
              {
                  return lhs.first < rhs.first;
              });

    for (wme* current = thisAgent->all_wmes_in_rete; current != NIL; current = current->rete_next)
    {
        std::string wme_sig = "tt=" + std::to_string(current->timetag) +
                              " acc=" + std::to_string(current->acceptable ? 1 : 0);
        if (current->id && current->id->is_sti())
        {
            wme_sig += " id=" + std::string(1, current->id->id->name_letter) + std::to_string(current->id->id->name_number);
        }
        else
        {
            wme_sig += " id=<non-id>";
        }
        if (current->value && current->value->is_sti())
        {
            wme_sig += " value=" + std::string(1, current->value->id->name_letter) + std::to_string(current->value->id->name_number);
        }
        else
        {
            wme_sig += " value=<non-id>";
        }
        detail.rete_wme_signatures.push_back(wme_sig);

        auto build_context = [&](const char* role)
        {
            std::string context = "tt=" + std::to_string(current->timetag) +
                                  " role=" + std::string(role) +
                                  " attr=" + symbol_debug_name(current->attr) +
                                  " value=" + symbol_debug_name(current->value) +
                                  " pref=";

            if (current->preference)
            {
                context += std::to_string(static_cast<int>(current->preference->type));
                if (current->preference->inst && current->preference->inst->prod)
                {
                    context += "/" + production_name_or_placeholder(current->preference->inst->prod);
                }
                else
                {
                    context += "/<arch-or-none>";
                }
            }
            else
            {
                context += "<none>";
            }

            return context;
        };

        if (current->id && current->id->is_sti())
        {
            append_identifier_wme_context(detail,
                                          encode_identifier_key(current->id->id->name_letter,
                                                                current->id->id->name_number),
                                          build_context("id"));
        }
        if (current->value && current->value->is_sti())
        {
            append_identifier_wme_context(detail,
                                          encode_identifier_key(current->value->id->name_letter,
                                                                current->value->id->name_number),
                                          build_context("value"));
        }
    }
    std::sort(detail.rete_wme_signatures.begin(), detail.rete_wme_signatures.end());
    std::sort(detail.identifier_wme_contexts.begin(),
              detail.identifier_wme_contexts.end(),
              [](const std::pair<uint64_t, std::string>& lhs, const std::pair<uint64_t, std::string>& rhs)
              {
                  if (lhs.first == rhs.first)
                  {
                      return lhs.second < rhs.second;
                  }
                  return lhs.first < rhs.first;
              });

    for (int type = 0; type < NUM_PRODUCTION_TYPES; ++type)
    {
        for (production* prod = thisAgent->all_productions_of_type[type]; prod != NIL; prod = prod->next)
        {
            if (!prod->instantiations)
            {
                continue;
            }

            uint64_t inst_count = 0;
            for (instantiation* inst = prod->instantiations; inst != NIL; inst = inst->next)
            {
                ++inst_count;
            }

            detail.match_instantiations_by_production.emplace_back(production_name_or_placeholder(prod), inst_count);
        }
    }

    std::sort(detail.match_instantiations_by_production.begin(),
              detail.match_instantiations_by_production.end(),
              [](const std::pair<std::string, uint64_t>& lhs, const std::pair<std::string, uint64_t>& rhs)
              {
                  if (lhs.first == rhs.first)
                  {
                      return lhs.second < rhs.second;
                  }
                  return lhs.first < rhs.first;
              });

    return detail;
}

void print_identifier_set_deltas(const SerializerDetailedSignature& expected,
                                 const SerializerDetailedSignature& actual)
{
    std::vector<uint64_t> missing_from_post_load;
    std::vector<uint64_t> added_after_post_load;

    size_t i = 0;
    size_t j = 0;
    while ((i < expected.identifier_keys.size()) || (j < actual.identifier_keys.size()))
    {
        if (j >= actual.identifier_keys.size() ||
            ((i < expected.identifier_keys.size()) && (expected.identifier_keys[i] < actual.identifier_keys[j])))
        {
            missing_from_post_load.push_back(expected.identifier_keys[i]);
            ++i;
        }
        else if (i >= expected.identifier_keys.size() || (actual.identifier_keys[j] < expected.identifier_keys[i]))
        {
            added_after_post_load.push_back(actual.identifier_keys[j]);
            ++j;
        }
        else
        {
            ++i;
            ++j;
        }
    }

    if (!missing_from_post_load.empty())
    {
        std::cerr << "[SERIALIZER_SIGNATURE] ids missing_after_load=" << missing_from_post_load.size();
        const size_t limit = std::min<size_t>(missing_from_post_load.size(), 16);
        for (size_t idx = 0; idx < limit; ++idx)
        {
            std::cerr << (idx == 0 ? " [" : ",") << format_identifier_key(missing_from_post_load[idx]);
        }
        if (missing_from_post_load.size() > limit)
        {
            std::cerr << ",...";
        }
        std::cerr << "]" << std::endl;
    }

    if (!added_after_post_load.empty())
    {
        std::cerr << "[SERIALIZER_SIGNATURE] ids added_after_load=" << added_after_post_load.size();
        const size_t limit = std::min<size_t>(added_after_post_load.size(), 16);
        for (size_t idx = 0; idx < limit; ++idx)
        {
            std::cerr << (idx == 0 ? " [" : ",") << format_identifier_key(added_after_post_load[idx]);
        }
        if (added_after_post_load.size() > limit)
        {
            std::cerr << ",...";
        }
        std::cerr << "]" << std::endl;
    }
}

void print_match_set_deltas(const SerializerDetailedSignature& expected,
                            const SerializerDetailedSignature& actual)
{
    size_t i = 0;
    size_t j = 0;
    size_t emitted = 0;
    const size_t kMaxLines = 24;

    while (((i < expected.match_instantiations_by_production.size()) ||
            (j < actual.match_instantiations_by_production.size())) &&
           (emitted < kMaxLines))
    {
        if (j >= actual.match_instantiations_by_production.size() ||
            ((i < expected.match_instantiations_by_production.size()) &&
             (expected.match_instantiations_by_production[i].first < actual.match_instantiations_by_production[j].first)))
        {
            std::cerr << "[SERIALIZER_SIGNATURE] match_delta prod="
                      << expected.match_instantiations_by_production[i].first
                      << " expected=" << expected.match_instantiations_by_production[i].second
                      << " actual=0" << std::endl;
            ++i;
            ++emitted;
        }
        else if (i >= expected.match_instantiations_by_production.size() ||
                 (actual.match_instantiations_by_production[j].first < expected.match_instantiations_by_production[i].first))
        {
            std::cerr << "[SERIALIZER_SIGNATURE] match_delta prod="
                      << actual.match_instantiations_by_production[j].first
                      << " expected=0"
                      << " actual=" << actual.match_instantiations_by_production[j].second << std::endl;
            ++j;
            ++emitted;
        }
        else
        {
            if (expected.match_instantiations_by_production[i].second !=
                actual.match_instantiations_by_production[j].second)
            {
                std::cerr << "[SERIALIZER_SIGNATURE] match_delta prod="
                          << expected.match_instantiations_by_production[i].first
                          << " expected=" << expected.match_instantiations_by_production[i].second
                          << " actual=" << actual.match_instantiations_by_production[j].second << std::endl;
                ++emitted;
            }
            ++i;
            ++j;
        }
    }
}

void print_identifier_field_hash_deltas(const SerializerDetailedSignature& expected,
                                        const SerializerDetailedSignature& actual)
{
    size_t i = 0;
    size_t j = 0;
    size_t emitted = 0;
    const size_t kMaxLines = 16;

    while (((i < expected.identifier_field_hashes.size()) || (j < actual.identifier_field_hashes.size())) &&
           (emitted < kMaxLines))
    {
        if (j >= actual.identifier_field_hashes.size() ||
            ((i < expected.identifier_field_hashes.size()) &&
             (expected.identifier_field_hashes[i].first < actual.identifier_field_hashes[j].first)))
        {
            std::cerr << "[SERIALIZER_SIGNATURE] id_field_delta id="
                      << format_identifier_key(expected.identifier_field_hashes[i].first)
                      << " expected_hash=" << expected.identifier_field_hashes[i].second
                      << " actual_hash=0" << std::endl;
            ++i;
            ++emitted;
        }
        else if (i >= expected.identifier_field_hashes.size() ||
                 (actual.identifier_field_hashes[j].first < expected.identifier_field_hashes[i].first))
        {
            std::cerr << "[SERIALIZER_SIGNATURE] id_field_delta id="
                      << format_identifier_key(actual.identifier_field_hashes[j].first)
                      << " expected_hash=0"
                      << " actual_hash=" << actual.identifier_field_hashes[j].second << std::endl;
            ++j;
            ++emitted;
        }
        else
        {
            if (expected.identifier_field_hashes[i].second != actual.identifier_field_hashes[j].second)
            {
                std::cerr << "[SERIALIZER_SIGNATURE] id_field_delta id="
                          << format_identifier_key(expected.identifier_field_hashes[i].first)
                          << " expected_hash=" << expected.identifier_field_hashes[i].second
                          << " actual_hash=" << actual.identifier_field_hashes[j].second << std::endl;
                ++emitted;
            }
            ++i;
            ++j;
        }
    }
}

void print_rete_wme_deltas(const SerializerDetailedSignature& expected,
                           const SerializerDetailedSignature& actual)
{
    size_t i = 0;
    size_t j = 0;
    size_t emitted = 0;
    const size_t kMaxLines = 16;

    while (((i < expected.rete_wme_signatures.size()) || (j < actual.rete_wme_signatures.size())) &&
           (emitted < kMaxLines))
    {
        if (j >= actual.rete_wme_signatures.size() ||
            ((i < expected.rete_wme_signatures.size()) &&
             (expected.rete_wme_signatures[i] < actual.rete_wme_signatures[j])))
        {
            std::cerr << "[SERIALIZER_SIGNATURE] rete_wme_missing "
                      << expected.rete_wme_signatures[i] << std::endl;
            ++i;
            ++emitted;
        }
        else if (i >= expected.rete_wme_signatures.size() ||
                 (actual.rete_wme_signatures[j] < expected.rete_wme_signatures[i]))
        {
            std::cerr << "[SERIALIZER_SIGNATURE] rete_wme_added "
                      << actual.rete_wme_signatures[j] << std::endl;
            ++j;
            ++emitted;
        }
        else
        {
            ++i;
            ++j;
        }
    }
}

void print_identifier_field_value_deltas(const SerializerDetailedSignature& expected,
                                         const SerializerDetailedSignature& actual)
{
    size_t i = 0;
    size_t j = 0;
    size_t emitted = 0;
    const size_t kMaxLines = 10;

    auto emit_field = [&](const std::string& field, uint64_t id_key, uint64_t expected_value, uint64_t actual_value)
    {
        std::cerr << "[SERIALIZER_SIGNATURE] id_field_value_delta id=" << format_identifier_key(id_key)
                  << " field=" << field
                  << " expected=" << expected_value
                  << " actual=" << actual_value << std::endl;
    };

    auto emit_wme_context = [&](const SerializerDetailedSignature& source,
                                const char* side,
                                uint64_t id_key)
    {
        const size_t kMaxContextLines = 3;
        auto it = std::lower_bound(source.identifier_wme_contexts.begin(),
                                   source.identifier_wme_contexts.end(),
                                   id_key,
                                   [](const std::pair<uint64_t, std::string>& entry, uint64_t key)
                                   {
                                       return entry.first < key;
                                   });

        size_t emitted_context = 0;
        while ((it != source.identifier_wme_contexts.end()) && (it->first == id_key) && (emitted_context < kMaxContextLines))
        {
            std::cerr << "[SERIALIZER_SIGNATURE] id_wme_context id=" << format_identifier_key(id_key)
                      << " side=" << side
                      << " " << it->second << std::endl;
            ++it;
            ++emitted_context;
        }

        if (emitted_context == 0)
        {
            std::cerr << "[SERIALIZER_SIGNATURE] id_wme_context id=" << format_identifier_key(id_key)
                      << " side=" << side
                      << " <none>" << std::endl;
        }
    };

    while ((i < expected.identifier_field_states.size()) &&
           (j < actual.identifier_field_states.size()) &&
           (emitted < kMaxLines))
    {
        if (expected.identifier_field_states[i].first < actual.identifier_field_states[j].first)
        {
            ++i;
            continue;
        }
        if (actual.identifier_field_states[j].first < expected.identifier_field_states[i].first)
        {
            ++j;
            continue;
        }

        const uint64_t key = expected.identifier_field_states[i].first;
        const auto& lhs = expected.identifier_field_states[i].second;
        const auto& rhs = actual.identifier_field_states[j].second;

        if (lhs.reference_count != rhs.reference_count)
        {
            emit_field("reference_count", key, lhs.reference_count, rhs.reference_count);
            emit_wme_context(expected, "expected", key);
            emit_wme_context(actual, "actual", key);
            ++emitted;
        }
        if ((emitted < kMaxLines) && (lhs.level != rhs.level))
        {
            emit_field("level", key, lhs.level, rhs.level);
            ++emitted;
        }
        if ((emitted < kMaxLines) && (lhs.link_count != rhs.link_count))
        {
            emit_field("link_count", key, lhs.link_count, rhs.link_count);
            emit_wme_context(expected, "expected", key);
            emit_wme_context(actual, "actual", key);
            ++emitted;
        }
        if ((emitted < kMaxLines) && (lhs.isa_goal != rhs.isa_goal))
        {
            emit_field("isa_goal", key, lhs.isa_goal ? 1 : 0, rhs.isa_goal ? 1 : 0);
            ++emitted;
        }
        if ((emitted < kMaxLines) && (lhs.isa_operator != rhs.isa_operator))
        {
            emit_field("isa_operator", key, lhs.isa_operator ? 1 : 0, rhs.isa_operator ? 1 : 0);
            ++emitted;
        }
        if ((emitted < kMaxLines) && (lhs.isa_impasse != rhs.isa_impasse))
        {
            emit_field("isa_impasse", key, lhs.isa_impasse ? 1 : 0, rhs.isa_impasse ? 1 : 0);
            ++emitted;
        }

        ++i;
        ++j;
    }
}

SerializerStateSignature build_serializer_state_signature(agent* thisAgent)
{
    SerializerStateSignature sig;

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

            ++sig.identifier_count;
            if (sym->id->isa_goal) ++sig.goal_count;
            if (sym->id->isa_operator) ++sig.operator_count;
            if (sym->id->isa_impasse) ++sig.impasse_count;
            if (sym->id->operator_slot) ++sig.ids_with_operator_slot;
            if (sym->id->associated_output_links) ++sig.ids_with_assoc_ol;
            if (sym->id->rl_info) ++sig.ids_with_rl;
            if (sym->id->epmem_info) ++sig.ids_with_epmem;
            if (sym->id->smem_info) ++sig.ids_with_smem;

            const uint64_t input_wmes = count_wme_list(sym->id->input_wmes);
            const uint64_t impasse_wmes = count_wme_list(sym->id->impasse_wmes);
            const uint64_t assoc_output_links = count_cons_list(sym->id->associated_output_links);

            if (input_wmes > 0) ++sig.ids_with_input_wmes;
            if (impasse_wmes > 0) ++sig.ids_with_impasse_wmes;

            sig.input_wmes += input_wmes;
            sig.impasse_wmes += impasse_wmes;
            sig.output_links += assoc_output_links;

            uint64_t id_slot_count = 0;
            uint64_t id_slot_wmes = 0;
            uint64_t id_slot_acceptable_wmes = 0;
            uint64_t id_slot_preferences = 0;
            for (slot* s = sym->id->slots; s != NIL; s = s->next)
            {
                ++id_slot_count;
                id_slot_wmes += count_wme_list(s->wmes);
                id_slot_acceptable_wmes += count_wme_list(s->acceptable_preference_wmes);
                id_slot_preferences += count_preference_list(s->all_preferences);
            }

            if (id_slot_count > 0)
            {
                ++sig.ids_with_slots;
            }

            sig.slot_count += id_slot_count;
            sig.slot_wmes += id_slot_wmes;
            sig.slot_acceptable_wmes += id_slot_acceptable_wmes;
            sig.slot_preferences += id_slot_preferences;

            mix_signature(sig.hash, static_cast<uint64_t>(sym->id->name_letter));
            mix_signature(sig.hash, sym->id->name_number);
            mix_signature(sig.hash, static_cast<uint64_t>(sym->reference_count));
            mix_signature(sig.hash, static_cast<uint64_t>(sym->id->level));
            mix_signature(sig.hash, sym->id->isa_goal ? 1 : 0);
            mix_signature(sig.hash, sym->id->isa_operator ? 1 : 0);
            mix_signature(sig.hash, sym->id->isa_impasse ? 1 : 0);
            mix_signature(sig.hash, sym->id->operator_slot ? 1 : 0);
            mix_signature(sig.hash, sym->id->higher_goal ? 1 : 0);
            mix_signature(sig.hash, sym->id->lower_goal ? 1 : 0);
            mix_signature(sig.hash, sym->id->rl_info ? 1 : 0);
            mix_signature(sig.hash, sym->id->epmem_info ? 1 : 0);
            mix_signature(sig.hash, sym->id->smem_info ? 1 : 0);
            mix_signature(sig.hash, sym->id->gds ? 1 : 0);
            mix_signature(sig.hash, id_slot_count);
            mix_signature(sig.hash, id_slot_wmes);
            mix_signature(sig.hash, id_slot_acceptable_wmes);
            mix_signature(sig.hash, id_slot_preferences);
            mix_signature(sig.hash, input_wmes);
            mix_signature(sig.hash, impasse_wmes);
            mix_signature(sig.hash, assoc_output_links);
            mix_signature(sig.hash, static_cast<uint64_t>(sym->id->link_count));
        }
    }

    for (wme* current = thisAgent->all_wmes_in_rete; current != NIL; current = current->rete_next)
    {
        ++sig.rete_wmes;
        mix_signature(sig.hash, current->acceptable ? 1 : 0);
        mix_signature(sig.hash, static_cast<uint64_t>(current->timetag));
        if (current->id && current->id->is_sti())
        {
            mix_signature(sig.hash, static_cast<uint64_t>(current->id->id->name_letter));
            mix_signature(sig.hash, current->id->id->name_number);
        }
        if (current->value && current->value->is_sti())
        {
            mix_signature(sig.hash, static_cast<uint64_t>(current->value->id->name_letter));
            mix_signature(sig.hash, current->value->id->name_number);
        }
    }

    sig.match_set_instantiations = count_match_set_instantiations(thisAgent, sig.match_set_productions);
    sig.ms_o_assertions = count_ms_level_list(thisAgent->ms_o_assertions);
    sig.ms_i_assertions = count_ms_level_list(thisAgent->ms_i_assertions);
    sig.ms_retractions = count_ms_level_list(thisAgent->ms_retractions);
    sig.postponed_assertions = count_ms_level_list(thisAgent->postponed_assertions);
    sig.nil_goal_retractions = count_ms_level_list(thisAgent->nil_goal_retractions);

    mix_signature(sig.hash, sig.identifier_count);
    mix_signature(sig.hash, sig.goal_count);
    mix_signature(sig.hash, sig.operator_count);
    mix_signature(sig.hash, sig.impasse_count);
    mix_signature(sig.hash, sig.ids_with_slots);
    mix_signature(sig.hash, sig.slot_count);
    mix_signature(sig.hash, sig.slot_wmes);
    mix_signature(sig.hash, sig.slot_acceptable_wmes);
    mix_signature(sig.hash, sig.slot_preferences);
    mix_signature(sig.hash, sig.ids_with_input_wmes);
    mix_signature(sig.hash, sig.input_wmes);
    mix_signature(sig.hash, sig.ids_with_impasse_wmes);
    mix_signature(sig.hash, sig.impasse_wmes);
    mix_signature(sig.hash, sig.ids_with_operator_slot);
    mix_signature(sig.hash, sig.ids_with_assoc_ol);
    mix_signature(sig.hash, sig.ids_with_rl);
    mix_signature(sig.hash, sig.ids_with_epmem);
    mix_signature(sig.hash, sig.ids_with_smem);
    mix_signature(sig.hash, sig.rete_wmes);
    mix_signature(sig.hash, sig.match_set_productions);
    mix_signature(sig.hash, sig.match_set_instantiations);
    mix_signature(sig.hash, sig.ms_o_assertions);
    mix_signature(sig.hash, sig.ms_i_assertions);
    mix_signature(sig.hash, sig.ms_retractions);
    mix_signature(sig.hash, sig.postponed_assertions);
    mix_signature(sig.hash, sig.nil_goal_retractions);
    mix_signature(sig.hash, sig.output_links);

    return sig;
}

void print_serializer_state_signature(const char* phase,
                                      const char* source_name,
                                      const SerializerStateSignature& sig)
{
    std::cerr << "[SERIALIZER_SIGNATURE] " << phase
              << " source=" << (source_name ? source_name : "<none>")
              << " hash=" << sig.hash
              << " ids=" << sig.identifier_count
              << " goals=" << sig.goal_count
              << " ops=" << sig.operator_count
              << " impasses=" << sig.impasse_count
              << " ids_with_slots=" << sig.ids_with_slots
              << " slots=" << sig.slot_count
              << " slot_wmes=" << sig.slot_wmes
              << " slot_acc_wmes=" << sig.slot_acceptable_wmes
              << " slot_prefs=" << sig.slot_preferences
              << " input_wmes=" << sig.input_wmes
              << " impasse_wmes=" << sig.impasse_wmes
              << " ids_with_op_slot=" << sig.ids_with_operator_slot
              << " ids_with_assoc_ol=" << sig.ids_with_assoc_ol
              << " ids_with_rl=" << sig.ids_with_rl
              << " ids_with_epmem=" << sig.ids_with_epmem
              << " ids_with_smem=" << sig.ids_with_smem
              << " rete_wmes=" << sig.rete_wmes
              << " match_prods=" << sig.match_set_productions
              << " match_insts=" << sig.match_set_instantiations
              << " ms_o=" << sig.ms_o_assertions
              << " ms_i=" << sig.ms_i_assertions
              << " ms_r=" << sig.ms_retractions
              << " postponed=" << sig.postponed_assertions
              << " nil_retract=" << sig.nil_goal_retractions
              << " output_links=" << sig.output_links
              << std::endl;
}

bool serializer_signatures_match(const SerializerStateSignature& expected,
                                 const SerializerStateSignature& actual)
{
    return (expected.hash == actual.hash) &&
           (expected.identifier_count == actual.identifier_count) &&
           (expected.goal_count == actual.goal_count) &&
           (expected.operator_count == actual.operator_count) &&
           (expected.impasse_count == actual.impasse_count) &&
           (expected.ids_with_slots == actual.ids_with_slots) &&
           (expected.slot_count == actual.slot_count) &&
           (expected.slot_wmes == actual.slot_wmes) &&
           (expected.slot_acceptable_wmes == actual.slot_acceptable_wmes) &&
           (expected.slot_preferences == actual.slot_preferences) &&
           (expected.input_wmes == actual.input_wmes) &&
           (expected.impasse_wmes == actual.impasse_wmes) &&
           (expected.ids_with_operator_slot == actual.ids_with_operator_slot) &&
           (expected.ids_with_assoc_ol == actual.ids_with_assoc_ol) &&
           (expected.ids_with_rl == actual.ids_with_rl) &&
           (expected.ids_with_epmem == actual.ids_with_epmem) &&
           (expected.ids_with_smem == actual.ids_with_smem) &&
           (expected.rete_wmes == actual.rete_wmes) &&
           (expected.match_set_productions == actual.match_set_productions) &&
           (expected.match_set_instantiations == actual.match_set_instantiations) &&
           (expected.ms_o_assertions == actual.ms_o_assertions) &&
           (expected.ms_i_assertions == actual.ms_i_assertions) &&
           (expected.ms_retractions == actual.ms_retractions) &&
           (expected.postponed_assertions == actual.postponed_assertions) &&
           (expected.nil_goal_retractions == actual.nil_goal_retractions) &&
           (expected.output_links == actual.output_links);
}

void maybe_capture_serializer_pre_save_signature(agent* thisAgent, const char* source_name)
{
    if (!serializer_signature_enabled() || !thisAgent)
    {
        return;
    }

    g_saved_serializer_signature.valid = true;
    g_saved_serializer_signature.agent_name = (thisAgent->name ? thisAgent->name : "");
    g_saved_serializer_signature.source_name = (source_name ? source_name : "<build_agent_state_message>");
    g_saved_serializer_signature.signature = build_serializer_state_signature(thisAgent);
    g_saved_serializer_signature.detail = build_serializer_detailed_signature(thisAgent);
    print_serializer_state_signature("pre-save", source_name, g_saved_serializer_signature.signature);
}

bool maybe_validate_serializer_post_load_signature(agent* thisAgent, const char* source_name)
{
    if (!serializer_signature_enabled() || !thisAgent || !source_name)
    {
        return true;
    }

    const SerializerStateSignature restored = build_serializer_state_signature(thisAgent);
    print_serializer_state_signature("post-load", source_name, restored);

    const std::string this_agent_name = (thisAgent->name ? thisAgent->name : "");
    if (!g_saved_serializer_signature.valid || (g_saved_serializer_signature.agent_name != this_agent_name))
    {
        std::cerr << "[SERIALIZER_SIGNATURE] compare skipped: no matching pre-save signature for agent="
                  << this_agent_name << " source=" << source_name << std::endl;
        return true;
    }

    print_serializer_state_signature("expected", g_saved_serializer_signature.source_name.c_str(), g_saved_serializer_signature.signature);
    const bool match = serializer_signatures_match(g_saved_serializer_signature.signature, restored);
    std::cerr << "[SERIALIZER_SIGNATURE] compare result=" << (match ? "MATCH" : "MISMATCH")
              << " source=" << source_name << std::endl;

    if (!match)
    {
        const SerializerDetailedSignature restored_detail = build_serializer_detailed_signature(thisAgent);
        print_identifier_set_deltas(g_saved_serializer_signature.detail, restored_detail);
        print_match_set_deltas(g_saved_serializer_signature.detail, restored_detail);
        print_identifier_field_hash_deltas(g_saved_serializer_signature.detail, restored_detail);
        print_identifier_field_value_deltas(g_saved_serializer_signature.detail, restored_detail);
        print_rete_wme_deltas(g_saved_serializer_signature.detail, restored_detail);
    }

    if (!match && serializer_signature_strict_enabled())
    {
        std::cerr << "[SERIALIZER_SIGNATURE] strict mode enabled: failing load due to state-signature mismatch"
                  << std::endl;
        return false;
    }

    return true;
}

}

bool build_agent_state_message(agent* thisAgent, soar::kernel::AgentState* state)
{
    if (!state)
    {
        return false;
    }

    state->Clear();
    state->set_agent_name(thisAgent->name ? thisAgent->name : "");

    const bool run_staged_validation = compute_staged_validation_for_next_save_cycle();
    set_current_save_cycle_staged_validation_enabled(run_staged_validation);

    SymbolIdMap symbol_map;

    auto validate_roundtrip = [&](const char* stage) -> bool
    {
        if (!run_staged_validation)
        {
            return true;
        }

        std::string encoded_state;
        if (!state->SerializeToString(&encoded_state))
        {
            thisAgent->outputManager->printa_sf(thisAgent,
                                                "Kernel state save aborted: AgentState encode failed during staged validation.\n");
            std::cerr << "[build_agent_state_message] staged encode failed at: " << stage << std::endl;
            return false;
        }

        soar::kernel::AgentState parsed_state;
        if (!parsed_state.ParseFromString(encoded_state))
        {
            soar::kernel::AgentState partial_state;
            const bool partial_ok = partial_state.ParsePartialFromString(encoded_state);
            thisAgent->outputManager->printa_sf(thisAgent,
                                                "Kernel state save aborted: AgentState failed staged validation parse.\n");
            std::cerr << "[build_agent_state_message] staged parse failed at: " << stage
                      << " bytes=" << encoded_state.size()
                      << " partial_parse=" << (partial_ok ? "true" : "false")
                      << std::endl;
            return false;
        }

        return true;
    };

    fill_state_settings(thisAgent, state, symbol_map);
    fill_state_counters(thisAgent, state);
    export_rng_state(state);
    gather_wmes(thisAgent, state, symbol_map);

    if (!validate_roundtrip("after gather_wmes"))
    {
        return false;
    }

    std::string liveness_error;
    if (!validate_runtime_symbol_liveness_for_save(thisAgent, liveness_error))
    {
        thisAgent->outputManager->printa_sf(thisAgent,
                                            "Kernel state save aborted: runtime liveness validation failed.\n");
        std::cerr << "[build_agent_state_message] liveness detail: " << liveness_error << std::endl;
        return false;
    }

    export_chunking_runtime_state(thisAgent, state, symbol_map);
    if (!validate_roundtrip("after export_chunking_runtime_state"))
    {
        return false;
    }

    export_active_goal_runtime_state(thisAgent, state, symbol_map);
    if (!validate_roundtrip("after export_active_goal_runtime_state"))
    {
        return false;
    }

    export_pending_ie_assertions(thisAgent, state, symbol_map);
    if (!validate_roundtrip("after export_pending_ie_assertions"))
    {
        return false;
    }

    export_pending_pe_assertions(thisAgent, state, symbol_map);
    if (!validate_roundtrip("after export_pending_pe_assertions"))
    {
        return false;
    }

    export_goal_identity_sets(thisAgent, state, symbol_map);
    if (!validate_roundtrip("after export_goal_identity_sets"))
    {
        return false;
    }

    export_goal_saved_firing_types(thisAgent, state, symbol_map);
    if (!validate_roundtrip("after export_goal_saved_firing_types"))
    {
        return false;
    }

    export_production_firing_counts(thisAgent, state);
    if (!validate_roundtrip("after export_production_firing_counts"))
    {
        return false;
    }

    export_production_rhs_identifier_patches(thisAgent, state, symbol_map);
    if (!validate_roundtrip("after export_production_rhs_identifier_patches"))
    {
        return false;
    }

    export_live_match_instantiations(thisAgent, state, symbol_map);
    if (!validate_roundtrip("after export_live_match_instantiations"))
    {
        return false;
    }

    if (!export_rete_network(thisAgent, state))
    {
        return false;
    }

    if (!validate_roundtrip("after export_rete_network"))
    {
        return false;
    }

    if (!export_semantic_memory(thisAgent, state))
    {
        return false;
    }

    if (!validate_roundtrip("after export_semantic_memory"))
    {
        return false;
    }

    export_all_identifier_symbols(thisAgent, state, symbol_map);

    if (!validate_roundtrip("after export_all_identifier_symbols"))
    {
        return false;
    }

    std::string encoded_state;
    if (!state->SerializeToString(&encoded_state))
    {
        thisAgent->outputManager->printa_sf(thisAgent,
                                            "Kernel state save aborted: unable to encode AgentState for validation.\n");
        return false;
    }

    soar::kernel::AgentState parsed_state;
    if (!parsed_state.ParseFromString(encoded_state))
    {
        soar::kernel::AgentState partial_state;
        const bool partial_ok = partial_state.ParsePartialFromString(encoded_state);
        thisAgent->outputManager->printa_sf(thisAgent,
                                            "Kernel state save aborted: AgentState failed validation parse.\n");
        std::cerr << "[build_agent_state_message] parse detail: bytes="
                  << encoded_state.size()
                  << " partial_parse=" << (partial_ok ? "true" : "false")
                  << std::endl;
        return false;
    }

    maybe_capture_serializer_pre_save_signature(thisAgent, "<build_agent_state_message>");

    return true;
}

bool serialize_agent_state(agent* thisAgent, const char* filename)
{
    soar::kernel::AgentState state;
    if (!build_agent_state_message(thisAgent, &state))
    {
        return false;
    }

    std::ofstream out(filename, std::ios::binary);
    if (!out.is_open())
    {
        thisAgent->outputManager->printa_sf(thisAgent, "Unable to open %s for writing.\n", filename);
        return false;
    }

    if (!state.SerializeToOstream(&out))
    {
        thisAgent->outputManager->printa_sf(thisAgent, "Failed to serialize agent state to %s.\n", filename);
        return false;
    }

    return true;
}

bool serializer_trace_enabled()
{
    return (std::getenv("SOAR_DEBUG_SERIALIZER_TRACE") != nullptr);
}

void serializer_trace_dump_goal_chain(agent* thisAgent, const char* phase)
{
    if (!serializer_trace_enabled() || !thisAgent)
    {
        return;
    }

    std::cerr << "[SERIALIZER_TRACE] " << phase
              << " top_goal=" << static_cast<void*>(thisAgent->top_goal)
              << " bottom_goal=" << static_cast<void*>(thisAgent->bottom_goal)
              << " top_state=" << static_cast<void*>(thisAgent->top_state)
              << std::endl;

    int chain_index = 0;
    for (Symbol* goal = thisAgent->top_goal; goal != NIL; goal = goal->id->lower_goal)
    {
        if (!goal->is_sti())
        {
            continue;
        }
        std::cerr << "[SERIALIZER_TRACE] " << phase
                  << " chain[" << chain_index++ << "]="
                  << goal->id->name_letter << goal->id->name_number
                  << " ptr=" << static_cast<void*>(goal)
                  << " level=" << goal->id->level
                  << " h=" << static_cast<void*>(goal->id->higher_goal)
                  << " l=" << static_cast<void*>(goal->id->lower_goal)
                  << std::endl;
    }

    Symbol* s1 = thisAgent->symbolManager->find_identifier('S', 1);
    if (s1 && s1->is_sti())
    {
        std::cerr << "[SERIALIZER_TRACE] " << phase
                  << " S1 ptr=" << static_cast<void*>(s1)
                  << " ref=" << s1->reference_count
                  << " goal=" << s1->id->isa_goal
                  << " level=" << s1->id->level
                  << " h=" << static_cast<void*>(s1->id->higher_goal)
                  << " l=" << static_cast<void*>(s1->id->lower_goal)
                  << " op_slot=" << static_cast<void*>(s1->id->operator_slot)
                  << " rl=" << static_cast<void*>(s1->id->rl_info)
                  << " epmem=" << static_cast<void*>(s1->id->epmem_info)
                  << " smem=" << static_cast<void*>(s1->id->smem_info)
                  << std::endl;
    }
    else
    {
        std::cerr << "[SERIALIZER_TRACE] " << phase << " S1 missing" << std::endl;
    }
}

bool restore_agent_state_message(agent* thisAgent, const soar::kernel::AgentState& state, const char* source_name)
{
    const char* source_label = source_name ? source_name : "embedded state";

    SavedProductionFiringCountMap saved_firing_counts = build_saved_production_firing_count_map(state);
    const bool saved_stopped = state.settings().system_halted();

    std::vector<int64_t> restored_trace_settings(HIGHEST_SYSPARAM_NUMBER + 1, 0);
    for (int index = 0; index < static_cast<int>(state.settings().trace_settings_size()) && index <= HIGHEST_SYSPARAM_NUMBER; ++index)
    {
        restored_trace_settings[index] = state.settings().trace_settings(index);
    }

    for (int index = 0; index <= HIGHEST_SYSPARAM_NUMBER; ++index)
    {
        thisAgent->trace_settings[index] = 0;
    }

    rebind_restore_critical_predefined_symbols(thisAgent);

    const bool prior_callback_suppression = thisAgent->suppress_callbacks_during_load;
    const bool prior_timers_enabled = thisAgent->timers_enabled;
    thisAgent->suppress_callbacks_during_load = true;
    thisAgent->timers_enabled = false;

    bool clear_restore_transient_queues = true;
    if (const char* clear_queues_env = std::getenv("SOAR_RESTORE_CLEAR_TRANSIENT_QUEUES"))
    {
        if ((clear_queues_env[0] == '0') ||
            !std::strcmp(clear_queues_env, "false") ||
            !std::strcmp(clear_queues_env, "off"))
        {
            clear_restore_transient_queues = false;
        }
    }

    if (clear_restore_transient_queues)
    {
        /* The snapshot test reset sequence (`soar init`, `production excise --all`,
           `run 1`) can leave transient buffered WM queues pointing at WMEs whose
           symbol payloads were freed during excise. Restore rebuilds WM from the
           serialized snapshot, so discard those pre-load queues before
           reinitialize_agent() walks them. */
        thisAgent->wmes_to_add = NIL;
        thisAgent->wmes_to_remove = NIL;
        thisAgent->ms_assertions = NIL;
        thisAgent->ms_retractions = NIL;
        thisAgent->ms_o_assertions = NIL;
        thisAgent->ms_i_assertions = NIL;
        thisAgent->postponed_assertions = NIL;
        thisAgent->nil_goal_retractions = NIL;
        thisAgent->output_link_changed = false;
    }

    bool skip_full_reinitialize = false;
    if (const char* skip_reinit_env = std::getenv("SOAR_RESTORE_SKIP_REINITIALIZE"))
    {
        if ((skip_reinit_env[0] == '1') ||
            !std::strcmp(skip_reinit_env, "true") ||
            !std::strcmp(skip_reinit_env, "on"))
        {
            skip_full_reinitialize = true;
        }
    }

    if (!skip_full_reinitialize)
    {
        serializer_trace_dump_goal_chain(thisAgent, "pre-purge-orphan-before-reinit");
        /* Snapshot restore can leave detached goal identifiers from prior
           runtime state. Release orphan goal runtime attachments before
           reinitialize_agent(), because its clear_goal_stack path only walks
           the active goal chain. */
        purge_orphan_goal_runtime_state(thisAgent);
        serializer_trace_dump_goal_chain(thisAgent, "post-purge-orphan-before-reinit");

        if (thisAgent->top_goal)
        {
            clear_goal_stack(thisAgent);
        }
        purge_orphan_goal_runtime_state(thisAgent);
        serializer_trace_dump_goal_chain(thisAgent, "post-explicit-clear-and-purge-before-reinit");

        for (Symbol* goal = thisAgent->top_goal; goal != NIL; goal = goal->id->lower_goal)
        {
            if (goal->is_sti() && goal->id->operator_slot)
            {
                goal->id->operator_slot->marked_for_possible_removal = false;
                mark_slot_for_possible_removal(thisAgent, goal->id->operator_slot);
            }
        }

        reinitialize_agent(thisAgent);
        serializer_trace_dump_goal_chain(thisAgent, "post-reinitialize");
    }
    else
    {
        clear_goal_stack(thisAgent);
        reset_wme_timetags(thisAgent);
        thisAgent->symbolManager->reset_hash_table(MP_identifier);
        thisAgent->symbolManager->reset_id_counters();
        if (thisAgent->SMem->connected())
        {
            thisAgent->SMem->reset_id_counters();
        }
    }

    if (!state.rete_binary().empty())
    {
        /* Preserve refs to common symbols before import_rete_network,
           since reteload_free_symbol_table will remove its temp refs. */
        std::vector<Symbol*> preserved_symbols;
        preserve_common_number_symbols(thisAgent, preserved_symbols);
        preserve_restore_critical_predefined_symbols(thisAgent, preserved_symbols);
        
        if (!import_rete_network(thisAgent, state.rete_binary()))
        {
            release_preserved_common_number_symbols(thisAgent, preserved_symbols);
            thisAgent->timers_enabled = prior_timers_enabled;
            thisAgent->suppress_callbacks_during_load = prior_callback_suppression;
            return false;
        }

        /* Keep preserved refs through bootstrap clear_goal_stack teardown.
           The imported rete initializes temporary state/WM that is immediately
           dismantled below, and that teardown can transiently touch these
           symbols via buffered structures. */
        if (thisAgent->top_goal)
        {
            clear_goal_stack(thisAgent);
        }
        release_preserved_common_number_symbols(thisAgent, preserved_symbols);
    }
    else if (thisAgent->top_goal)
    {
        /* No imported rete bootstrap in this path, but still clear live goals
           before rebuilding runtime structures from serialized state. */
        clear_goal_stack(thisAgent);
    }

    thisAgent->top_goal = NIL;
    thisAgent->bottom_goal = NIL;
    thisAgent->top_state = NIL;
    thisAgent->highest_goal_whose_context_changed = NIL;
    reset_transient_agent_state(thisAgent);

    SymbolReverseMap symbol_map;
    OwnedSymbolRefs owned_symbol_refs;

    if (state.rng_state_size() > 0)
    {
        std::vector<uint32_t> rng_state_words;
        rng_state_words.reserve(static_cast<size_t>(state.rng_state_size()));
        for (int i = 0; i < state.rng_state_size(); ++i)
        {
            rng_state_words.push_back(state.rng_state(i));
        }

        if (!SoarLoadRNGState(rng_state_words.data(), static_cast<uint32_t>(rng_state_words.size())))
        {
            thisAgent->outputManager->printa_sf(thisAgent,
                                                "Warning: RNG state payload invalid while loading %s; using current RNG state.\n",
                                                source_label);
        }
    }

    const bool debug_refcount_phase_delta = (std::getenv("SOAR_DEBUG_REFCOUNT_PHASE_DELTA") != nullptr);
    const bool debug_live_match_ref_step = (std::getenv("SOAR_DEBUG_REFCOUNT_LIVE_MATCH_STEP") != nullptr);
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
        {'O', 5},
        {'O', 6},
        {'O', 7},
        {'S', 1},
        {'S', 2},
    };
    uint64_t previous_tracked_refs[sizeof(tracked_ref_ids) / sizeof(tracked_ref_ids[0])] = {0};
    bool have_previous_tracked_refs = false;

    auto emit_refcount_phase_delta = [&](const char* phase)
    {
        if (!debug_refcount_phase_delta)
        {
            return;
        }

        std::fprintf(stderr, "restore_refcount_phase: %s", phase);
        for (size_t i = 0; i < (sizeof(tracked_ref_ids) / sizeof(tracked_ref_ids[0])); ++i)
        {
            const auto& tracked = tracked_ref_ids[i];
            Symbol* sym = thisAgent->symbolManager->find_identifier(tracked.letter, tracked.number);
            const uint64_t current_ref = (sym && sym->is_sti()) ? sym->reference_count : 0;
            const long long delta = have_previous_tracked_refs
                ? static_cast<long long>(current_ref) - static_cast<long long>(previous_tracked_refs[i])
                : 0;

            std::fprintf(stderr,
                         " %c%llu=%llu",
                         tracked.letter,
                         static_cast<unsigned long long>(tracked.number),
                         static_cast<unsigned long long>(current_ref));
            if (have_previous_tracked_refs)
            {
                std::fprintf(stderr, "(%+lld)", delta);
            }

            previous_tracked_refs[i] = current_ref;
        }
        std::fprintf(stderr, "\n");
        have_previous_tracked_refs = true;
    };

    auto capture_tracked_refcounts = [&](uint64_t* out_refs)
    {
        for (size_t i = 0; i < (sizeof(tracked_ref_ids) / sizeof(tracked_ref_ids[0])); ++i)
        {
            const auto& tracked = tracked_ref_ids[i];
            Symbol* sym = thisAgent->symbolManager->find_identifier(tracked.letter, tracked.number);
            out_refs[i] = (sym && sym->is_sti()) ? sym->reference_count : 0;
        }
    };

    auto emit_live_match_step_delta = [&](const char* stage, instantiation* inst, const uint64_t* before_refs)
    {
        if (!debug_live_match_ref_step)
        {
            return;
        }

        const char* prod_name = "<nil>";
        if (inst && inst->prod_name && inst->prod_name->is_string())
        {
            prod_name = inst->prod_name->sc->name;
        }

        std::fprintf(stderr,
                     "restore_live_match_step: %s prod=%s rete=%d",
                     stage,
                     prod_name,
                     (inst && inst->rete_token) ? 1 : 0);

        bool any_change = false;
        for (size_t i = 0; i < (sizeof(tracked_ref_ids) / sizeof(tracked_ref_ids[0])); ++i)
        {
            const auto& tracked = tracked_ref_ids[i];
            Symbol* sym = thisAgent->symbolManager->find_identifier(tracked.letter, tracked.number);
            const uint64_t after_ref = (sym && sym->is_sti()) ? sym->reference_count : 0;
            const long long delta = static_cast<long long>(after_ref) - static_cast<long long>(before_refs[i]);
            if (delta == 0)
            {
                continue;
            }

            any_change = true;
            std::fprintf(stderr,
                         " %c%llu:%llu(%+lld)",
                         tracked.letter,
                         static_cast<unsigned long long>(tracked.number),
                         static_cast<unsigned long long>(after_ref),
                         delta);
        }

        if (!any_change)
        {
            std::fprintf(stderr, " no-change");
        }
        std::fprintf(stderr, "\n");
    };

    emit_refcount_phase_delta("start");

    if (!build_symbol_map(thisAgent, state, symbol_map, owned_symbol_refs))
    {
        release_owned_symbol_refs(thisAgent, owned_symbol_refs);
        thisAgent->outputManager->printa_sf(thisAgent, "Failed to rebuild symbols while loading %s.\n", source_label);
        thisAgent->timers_enabled = prior_timers_enabled;
        thisAgent->suppress_callbacks_during_load = prior_callback_suppression;
        return false;
    }

    emit_refcount_phase_delta("post-build-symbol-map");

    patch_loaded_production_rhs_identifiers(thisAgent, state, symbol_map);

    set_agent_symbol_fields(thisAgent, state, symbol_map);
    emit_refcount_phase_delta("post-set-agent-symbol-fields");
    serializer_trace_dump_goal_chain(thisAgent, "post-set-agent-symbol-fields");
    rebuild_goal_chain(thisAgent, state, symbol_map);
    emit_refcount_phase_delta("post-rebuild-goal-chain");
    serializer_trace_dump_goal_chain(thisAgent, "post-rebuild-goal-chain");
    purge_orphan_goal_runtime_state(thisAgent);
    emit_refcount_phase_delta("post-purge-orphan-goal-runtime");
    serializer_trace_dump_goal_chain(thisAgent, "post-purge-orphan-after-rebuild");

    if (state.has_chunking_runtime())
    {
        restore_chunking_runtime_state(thisAgent, state.chunking_runtime(), symbol_map);
    }
    else
    {
        restore_chunking_runtime_state(thisAgent, state, symbol_map);
    }
    RestoredActiveGoalRuntimeState restored_active_goal_state = state.has_active_goal_runtime()
        ? restore_active_goal_runtime_state(state.active_goal_runtime(), symbol_map)
        : restore_active_goal_runtime_state(state, symbol_map);

    uint64_t max_restored_identity_id = 0;
    uint64_t max_restored_inst_identity_id = 0;
    const bool restore_goal_identity_state = (state.live_match_instantiations_size() == 0);
    if (restore_goal_identity_state && (state.goal_identity_sets_size() > 0))
    {
        restore_goal_identity_sets(thisAgent,
                                   state.goal_identity_sets(),
                                   symbol_map,
                                   max_restored_identity_id,
                                   max_restored_inst_identity_id);
    }
    if (restore_goal_identity_state && state.has_chunking_runtime())
    {
        restore_inst_identity_map(thisAgent, state.chunking_runtime());
    }
    if (state.goal_saved_firing_types_size() > 0)
    {
        restore_goal_saved_firing_types(thisAgent, state.goal_saved_firing_types(), symbol_map);
    }

    top_level_phase saved_phase = static_cast<top_level_phase>(state.settings().current_phase());
    thisAgent->applyPhase = (saved_phase == APPLY_PHASE);
    thisAgent->FIRING_TYPE = thisAgent->applyPhase ? PE_PRODS : IE_PRODS;

    if (thisAgent->top_state)
    {
        thisAgent->active_goal = thisAgent->top_state;
        thisAgent->highest_active_goal = thisAgent->top_state;
        thisAgent->active_level = thisAgent->top_state->id->level;
    }

    thisAgent->stop_soar = true;
    thisAgent->reason_for_stopping = "Restoring agent state quietly - elaborations suppressed.";

    RestoredPreferenceMap preference_map;
    RestoredPreferenceCloneMap preference_clone_map;
    SyntheticPreferenceLifecycle synthetic_preference_lifecycle;
    create_wmes_from_entries(thisAgent,
                             state.active_wmes(),
                             symbol_map,
                             preference_map,
                             preference_clone_map,
                             &synthetic_preference_lifecycle);
    emit_refcount_phase_delta("post-create-wmes");
    if (!saved_stopped)
    {
        /* Buffered add/remove lists are transient queue state and can contain
           stale ownership links across repeated snapshot restore cycles.
           Rebuild from active WM and reified matches instead of replaying
           serialized buffered deltas for non-halted saves. */
        if (thisAgent->wmes_to_add || thisAgent->wmes_to_remove)
        {
            do_buffered_wm_and_ownership_changes(thisAgent);
        }
    }
    else if (thisAgent->wmes_to_add || thisAgent->wmes_to_remove)
    {
        do_buffered_wm_and_ownership_changes(thisAgent);
    }

    rebuild_output_link_state(thisAgent, state.active_wmes());
    emit_refcount_phase_delta("post-rebuild-output-link-state");
    synchronize_identifier_counters_from_live_state(thisAgent);

    /* WME insertion during restore drives RETE and can populate match-set lists
       before saved pending assertions are restored. Clear those transient entries
       so pending IE/PE assertions come only from serialized state. */
    purge_restored_match_set(thisAgent);

    if (state.has_chunking_runtime())
    {
        restore_slot_osk_prefs(thisAgent,
                               state.chunking_runtime(),
                               symbol_map,
                               preference_map,
                               preference_clone_map);
        emit_refcount_phase_delta("post-initial-restore-slot-osk-prefs");
    }
    rebuild_io_header_state(thisAgent);
    emit_refcount_phase_delta("post-rebuild-io-header-state");
    bool skip_goal_module_rebuild = false;
    if (const char* skip_goal_module_env = std::getenv("SOAR_RESTORE_SKIP_GOAL_MODULE_WME_REBUILD"))
    {
        if ((skip_goal_module_env[0] == '1') ||
            !std::strcmp(skip_goal_module_env, "true") ||
            !std::strcmp(skip_goal_module_env, "on"))
        {
            skip_goal_module_rebuild = true;
        }
    }
    if (!skip_goal_module_rebuild)
    {
        rebuild_goal_module_wme_state(thisAgent);
        emit_refcount_phase_delta("post-rebuild-goal-module-wme-state");
    }
    const bool restore_pending_assertions = true;
    if (restore_pending_assertions)
    {
        if (state.pending_ie_assertions_size() > 0)
        {
            restore_pending_ie_assertions(thisAgent, state.pending_ie_assertions(), symbol_map);
        }
        else
        {
            restore_pending_ie_assertions(thisAgent, state, symbol_map);
        }
        if (state.pending_pe_assertions_size() > 0)
        {
            restore_pending_pe_assertions(thisAgent, state.pending_pe_assertions(), symbol_map);
        }
    }

    bool use_live_match_restore = true;
    if (const char* restore_live_matches_env = std::getenv("SOAR_RESTORE_LIVE_MATCHES"))
    {
        if ((restore_live_matches_env[0] == '0') ||
            !std::strcmp(restore_live_matches_env, "false") ||
            !std::strcmp(restore_live_matches_env, "off"))
        {
            use_live_match_restore = false;
        }
    }

    std::unordered_map<uint64_t, instantiation*> restored_instantiation_id_map;
    if ((state.live_match_instantiations_size() > 0) && use_live_match_restore)
    {
        std::vector<instantiation*> restored_instantiations;
        if (!restore_live_match_instantiations(thisAgent,
                                               state,
                                               symbol_map,
                                               preference_map,
                                               preference_clone_map,
                                               &restored_instantiations,
                                               &restored_instantiation_id_map))
        {
            reify_current_matches_for_fired_productions(thisAgent, saved_firing_counts);
        }

        if (state.has_chunking_runtime())
        {
            emit_refcount_phase_delta("pre-clear-restored-slot-osk-prefs");
            clear_restored_slot_osk_prefs(thisAgent, state.chunking_runtime(), symbol_map);
            emit_refcount_phase_delta("post-clear-restored-slot-osk-prefs");
            restore_slot_osk_prefs(thisAgent,
                                   state.chunking_runtime(),
                                   symbol_map,
                                   preference_map,
                                   preference_clone_map);
            emit_refcount_phase_delta("post-second-restore-slot-osk-prefs");

            for (instantiation* restored_inst : restored_instantiations)
            {
                if (!restored_inst)
                {
                    continue;
                }

                if (!restored_inst->rete_token)
                {
                    repair_synthetic_saved_condition_traces(thisAgent, restored_inst, preference_map);

                    if (!restored_inst->OSK_prefs)
                    {
                        uint64_t before_refs[sizeof(tracked_ref_ids) / sizeof(tracked_ref_ids[0])] = {0};
                        capture_tracked_refcounts(before_refs);
                        refresh_synthetic_instantiation_osk_from_slots(thisAgent, restored_inst);
                        emit_live_match_step_delta("post-refresh-synth-osk", restored_inst, before_refs);
                    }
                }
                else
                {
                    if (restored_inst->OSK_prefs)
                    {
                        clear_preference_list(thisAgent, restored_inst->OSK_prefs);
                        restored_inst->OSK_prefs = NIL;
                    }

                    uint64_t before_refs[sizeof(tracked_ref_ids) / sizeof(tracked_ref_ids[0])] = {0};
                    capture_tracked_refcounts(before_refs);
                    thisAgent->explanationBasedChunker->copy_OSK(restored_inst);
                    emit_live_match_step_delta("post-copy-osk", restored_inst, before_refs);
                }

            }
        }
    }
    else
    {
        reify_current_matches_for_fired_productions(thisAgent, saved_firing_counts);
    }

    rebind_restored_wme_preferences_by_provenance(thisAgent,
                                                  state.active_wmes(),
                                                  restored_instantiation_id_map);
    emit_refcount_phase_delta("post-restore-live-matches");

    if (!restore_pending_assertions)
    {
        reify_pending_matches(thisAgent);
    }

    rebuild_restored_wma_o_sets(thisAgent, preference_map);

    /* Release the extra symbol refs added by add_restored_preference_refs during
       create_wmes_from_entries.  These refs kept identifiers alive across the
       restore pipeline; without this call they accumulate indefinitely, one set
       per restore cycle, producing growing refcount leaks on WM identifiers. */
    deallocate_restored_preferences(thisAgent, preference_map);
    emit_refcount_phase_delta("post-deallocate-restored-preference-map");
    deallocate_synthetic_restored_preferences(thisAgent, synthetic_preference_lifecycle);
    emit_refcount_phase_delta("post-deallocate-synthetic-restored-preferences");

    clear_restored_slot_change_tracking(thisAgent);

    const bool restored_pending_ie_explicit = (state.pending_ie_assertions_size() > 0);
    const bool restored_pending_pe_explicit = (state.pending_pe_assertions_size() > 0);
    finalize_restored_runtime_state(thisAgent,
                                    saved_phase,
                                    saved_stopped,
                                    state.settings().input_cycle_flag(),
                                    restored_pending_ie_explicit,
                                    restored_pending_pe_explicit,
                                    saved_firing_counts);
    emit_refcount_phase_delta("post-finalize-runtime");

    rebuild_restored_context_slot_change_tracking(thisAgent, saved_phase);

    if (saved_phase == APPLY_PHASE && restored_active_goal_state.valid)
    {
        Symbol* pending_activity_goal = highest_restored_apply_activity_goal(thisAgent);
        if (pending_activity_goal && (pending_activity_goal != restored_active_goal_state.active_goal))
        {
            restored_active_goal_state.valid = false;
        }
    }

    if (restored_active_goal_state.valid)
    {
        thisAgent->active_goal = restored_active_goal_state.active_goal;
        thisAgent->previous_active_goal = restored_active_goal_state.previous_active_goal;
        thisAgent->highest_active_goal = restored_active_goal_state.highest_active_goal;
        thisAgent->active_level = restored_active_goal_state.active_level;
        thisAgent->previous_active_level = restored_active_goal_state.previous_active_level;
        thisAgent->highest_active_level = restored_active_goal_state.highest_active_level;
        thisAgent->change_level = restored_active_goal_state.change_level;
        thisAgent->next_change_level = restored_active_goal_state.next_change_level;
    }

    if (!restored_active_goal_state.valid)
    {
        if (saved_phase == APPLY_PHASE)
        {
            determine_highest_active_production_level_in_stack_apply(thisAgent);
        }
        else
        {
            determine_highest_active_production_level_in_stack_propose(thisAgent);
        }
    }

    thisAgent->current_phase = saved_phase;
    thisAgent->applyPhase = (saved_phase == APPLY_PHASE);
    thisAgent->FIRING_TYPE = thisAgent->applyPhase ? PE_PRODS : IE_PRODS;

    if (state.settings().current_wme_timetag() != 0)
    {
        thisAgent->current_wme_timetag = state.settings().current_wme_timetag();
    }
    apply_state_counters(thisAgent, state);

    rebuild_output_link_transient_state_from_tc(thisAgent);

    uint64_t restored_timers_cpu_t1 = 0;
    uint64_t restored_timers_cpu_elapsed = 0;
    double restored_timers_cpu_raw_per_usec = 0.0;
    bool restored_timers_cpu_running = false;
    thisAgent->timers_cpu.export_state(restored_timers_cpu_t1,
                                       restored_timers_cpu_elapsed,
                                       restored_timers_cpu_raw_per_usec,
                                       restored_timers_cpu_running);

    uint64_t restored_timers_kernel_t1 = 0;
    uint64_t restored_timers_kernel_elapsed = 0;
    double restored_timers_kernel_raw_per_usec = 0.0;
    bool restored_timers_kernel_running = false;
    thisAgent->timers_kernel.export_state(restored_timers_kernel_t1,
                                          restored_timers_kernel_elapsed,
                                          restored_timers_kernel_raw_per_usec,
                                          restored_timers_kernel_running);

    uint64_t restored_timers_phase_t1 = 0;
    uint64_t restored_timers_phase_elapsed = 0;
    double restored_timers_phase_raw_per_usec = 0.0;
    bool restored_timers_phase_running = false;
    thisAgent->timers_phase.export_state(restored_timers_phase_t1,
                                         restored_timers_phase_elapsed,
                                         restored_timers_phase_raw_per_usec,
                                         restored_timers_phase_running);

    const uint64_t restored_timers_total_cpu_usec = thisAgent->timers_total_cpu_time.get_usec();
    const uint64_t restored_timers_total_kernel_usec = thisAgent->timers_total_kernel_time.get_usec();
    const uint64_t restored_timers_input_usec = thisAgent->timers_input_function_cpu_time.get_usec();
    const uint64_t restored_timers_output_usec = thisAgent->timers_output_function_cpu_time.get_usec();

    uint64_t restored_phase_timer_usec[NUM_PHASE_TYPES];
    uint64_t restored_callback_timer_usec[NUMBER_OF_CALLBACKS];
    for (int i = 0; i < NUM_PHASE_TYPES; ++i)
    {
        restored_phase_timer_usec[i] = thisAgent->timers_decision_cycle_phase[i].get_usec();
    }
    for (int i = 0; i < NUMBER_OF_CALLBACKS; ++i)
    {
        restored_callback_timer_usec[i] = thisAgent->callback_timers[i].get_usec();
    }

    apply_production_firing_counts(thisAgent, state);
    emit_refcount_phase_delta("post-apply-firing-counts");

    for (int index = 0; index <= HIGHEST_SYSPARAM_NUMBER; ++index)
    {
        thisAgent->trace_settings[index] = restored_trace_settings[index];
    }

    thisAgent->stop_soar = state.settings().stop_soar();
    set_restored_reason_for_stopping(thisAgent, state.settings().reason_for_stopping());

    if (state.smem_enabled() && thisAgent->SMem->enabled())
    {
        thisAgent->outputManager->printa_sf(thisAgent, "Saved semantic memory was present in state file, but automatic SMem import is not available.\n");
    }

    std::string restore_pref_integrity_error;
    if (restore_preference_validation_enabled() &&
        !validate_restore_preference_integrity(thisAgent, restore_pref_integrity_error))
    {
        thisAgent->outputManager->printa_sf(thisAgent,
                                            "Kernel state load aborted: restored preference integrity validation failed.\n");
        std::cerr << "[restore_agent_state_message] preference integrity detail: "
                  << restore_pref_integrity_error << std::endl;
        release_owned_symbol_refs(thisAgent, owned_symbol_refs);
        thisAgent->timers_enabled = prior_timers_enabled;
        thisAgent->suppress_callbacks_during_load = prior_callback_suppression;
        return false;
    }

    release_owned_symbol_refs(thisAgent, owned_symbol_refs);
    emit_refcount_phase_delta("post-release-owned-symbol-refs");

    ensure_common_int_constants_exist_0_to_100(thisAgent);
    rebind_restore_critical_predefined_symbols(thisAgent);

    if (!maybe_validate_serializer_post_load_signature(thisAgent, source_name))
    {
        thisAgent->timers_enabled = prior_timers_enabled;
        thisAgent->suppress_callbacks_during_load = prior_callback_suppression;
        return false;
    }

    /* Canonicalize transient RETE save/load bookkeeping so post-load runtime
       shape matches vanilla execution state more closely. */
    thisAgent->reteload_num_ams = 0;
    thisAgent->reteload_am_table = NIL;
    thisAgent->reteload_num_syms = 0;
    thisAgent->reteload_symbol_table = NIL;
    thisAgent->current_retesave_amindex = 0;
    thisAgent->current_retesave_symindex = 0;
    thisAgent->symbolManager->clear_retesave_symbol_indices();

    for (uint64_t i = 0; i < 16; ++i)
    {
        do_for_all_items_in_hash_table(thisAgent,
                                       thisAgent->alpha_hash_tables[i],
                                       reset_rete_alpha_mem_retesave_index,
                                       NIL);
    }

    thisAgent->timers_cpu.import_state(restored_timers_cpu_t1,
                                       restored_timers_cpu_elapsed,
                                                    restored_timers_cpu_raw_per_usec,
                                                    restored_timers_cpu_running);
    thisAgent->timers_kernel.import_state(restored_timers_kernel_t1,
                                          restored_timers_kernel_elapsed,
                                                        restored_timers_kernel_raw_per_usec,
                                                        restored_timers_kernel_running);
    thisAgent->timers_phase.import_state(restored_timers_phase_t1,
                                         restored_timers_phase_elapsed,
                                                      restored_timers_phase_raw_per_usec,
                                                      restored_timers_phase_running);
    thisAgent->timers_total_cpu_time.set_usec(restored_timers_total_cpu_usec);
    thisAgent->timers_total_kernel_time.set_usec(restored_timers_total_kernel_usec);
    thisAgent->timers_input_function_cpu_time.set_usec(restored_timers_input_usec);
    thisAgent->timers_output_function_cpu_time.set_usec(restored_timers_output_usec);
    for (int i = 0; i < NUM_PHASE_TYPES; ++i)
    {
        thisAgent->timers_decision_cycle_phase[i].set_usec(restored_phase_timer_usec[i]);
    }
    for (int i = 0; i < NUMBER_OF_CALLBACKS; ++i)
    {
        thisAgent->callback_timers[i].set_usec(restored_callback_timer_usec[i]);
    }

    thisAgent->timers_enabled = prior_timers_enabled;
    thisAgent->suppress_callbacks_during_load = prior_callback_suppression;

    if (!thisAgent->reason_for_stopping)
    {
        thisAgent->reason_for_stopping = "";
    }

    return true;
}

bool deserialize_agent_state(agent* thisAgent, const char* filename)
{
    soar::kernel::AgentState state;
    std::ifstream in(filename, std::ios::binary);
    if (!in.is_open())
    {
        thisAgent->outputManager->printa_sf(thisAgent, "Unable to open %s for reading.\n", filename);
        return false;
    }

    if (!state.ParseFromIstream(&in))
    {
        thisAgent->outputManager->printa_sf(thisAgent, "Failed to parse agent state from %s.\n", filename);
        return false;
    }

    return restore_agent_state_message(thisAgent, state, filename);
}

Symbol* saveAgentState_rhs(agent* thisAgent, cons* args, void* /*user_data*/)
{
    if (args == NIL)
    {
        thisAgent->outputManager->printa_sf(thisAgent, "(saveAgentState) requires a filename argument.\n");
        return NIL;
    }

    Symbol* filename_symbol = static_cast<Symbol*>(args->first);
    const char* filename = filename_symbol->to_string();
    if (!serialize_agent_state(thisAgent, filename))
    {
        return NIL;
    }
    return thisAgent->symbolManager->make_str_constant(filename);
}

Symbol* loadAgentState_rhs(agent* thisAgent, cons* args, void* /*user_data*/)
{
    if (args == NIL)
    {
        thisAgent->outputManager->printa_sf(thisAgent, "(loadAgentState) requires a filename argument.\n");
        return NIL;
    }

    Symbol* filename_symbol = static_cast<Symbol*>(args->first);
    const char* filename = filename_symbol->to_string();
    if (!deserialize_agent_state(thisAgent, filename))
    {
        return NIL;
    }
    return thisAgent->symbolManager->make_str_constant(filename);
}

}
}
