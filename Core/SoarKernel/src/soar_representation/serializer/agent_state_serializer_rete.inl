bool export_rete_network(agent* thisAgent, soar::kernel::AgentState* state)
{
    FILE* rete_file = std::tmpfile();
    if (!rete_file)
    {
        thisAgent->outputManager->printa_sf(thisAgent, "Unable to create a temporary file for embedded rete serialization.\n");
        return false;
    }

    bool ok = save_rete_net(thisAgent, rete_file, true, true);
    if (!ok)
    {
        std::fclose(rete_file);
        thisAgent->outputManager->printa_sf(thisAgent, "Failed to embed productions in the agent state snapshot.\n");
        return false;
    }

    if (std::fflush(rete_file) != 0 || std::fseek(rete_file, 0, SEEK_END) != 0)
    {
        std::fclose(rete_file);
        thisAgent->outputManager->printa_sf(thisAgent, "Failed to finalize the embedded rete snapshot.\n");
        return false;
    }

    long rete_size = std::ftell(rete_file);
    if (rete_size < 0)
    {
        std::fclose(rete_file);
        thisAgent->outputManager->printa_sf(thisAgent, "Failed to measure the embedded rete snapshot.\n");
        return false;
    }

    if (std::fseek(rete_file, 0, SEEK_SET) != 0)
    {
        std::fclose(rete_file);
        thisAgent->outputManager->printa_sf(thisAgent, "Failed to rewind the embedded rete snapshot.\n");
        return false;
    }

    std::string rete_binary(static_cast<size_t>(rete_size), '\0');
    if (!rete_binary.empty())
    {
        size_t bytes_read = std::fread(&rete_binary[0], 1, rete_binary.size(), rete_file);
        if (bytes_read != rete_binary.size())
        {
            std::fclose(rete_file);
            thisAgent->outputManager->printa_sf(thisAgent, "Failed to read the embedded rete snapshot.\n");
            return false;
        }
    }

    std::fclose(rete_file);
    state->set_rete_binary(rete_binary);
    return true;
}

bool import_rete_network(agent* thisAgent, const std::string& rete_binary)
{
    FILE* rete_file = std::tmpfile();
    if (!rete_file)
    {
        thisAgent->outputManager->printa_sf(thisAgent, "Unable to create a temporary file for embedded rete restoration.\n");
        return false;
    }

    if (!rete_binary.empty())
    {
        size_t bytes_written = std::fwrite(rete_binary.data(), 1, rete_binary.size(), rete_file);
        if (bytes_written != rete_binary.size())
        {
            std::fclose(rete_file);
            thisAgent->outputManager->printa_sf(thisAgent, "Failed to stage the embedded rete snapshot for loading.\n");
            return false;
        }
    }

    if (std::fflush(rete_file) != 0 || std::fseek(rete_file, 0, SEEK_SET) != 0)
    {
        std::fclose(rete_file);
        thisAgent->outputManager->printa_sf(thisAgent, "Failed to rewind the embedded rete snapshot for loading.\n");
        return false;
    }

    bool ok = load_rete_net(thisAgent, rete_file);
    std::fclose(rete_file);
    if (!ok)
    {
        thisAgent->outputManager->printa_sf(thisAgent, "Failed to restore productions from the embedded rete snapshot.\n");
            std::cerr << "[import_rete_network] load_rete_net returned false" << std::endl;
        return false;
    }

    return true;
}