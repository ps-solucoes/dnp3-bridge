#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dnp3sim {

constexpr std::array<const char*, 11> kBinaryInputNames = {{
    "Equipamento Energizado",
    "Equipamento Operando",
    "Alarme",
    "Operacao Local/Remoto",
    "Botoeira de Emergencia",
    "Status Desequilibrio",
    "Status Reativo",
    "Status Suporte de Tensao",
    "Status Regulacao de Tensao",
    "Status Compensacao Harmonica",
    "Erro de IGBT",
}};

constexpr std::array<const char*, 20> kBinaryOutputNames = {{
    "Conectar Equipamento",
    "Desconectar Equipamento",
    "Reset Protecao",
    "Emergencia",
    "Ativa Desequilibrio",
    "Desativa Desequilibrio",
    "Ativa Suporte de Tensao",
    "Desativa Suporte de Tensao",
    "Ativa Regulacao de Tensao",
    "Desativa Regulacao de Tensao",
    "Ativa Compensacao Harmonica",
    "Desativa Compensacao Harmonica",
    "Compensa Harmonica 3",
    "Compensa Harmonica 5",
    "Compensa Harmonica 7",
    "Compensa Harmonica 9",
    "Compensa Harmonica 11",
    "Compensacao Harmonica Completa",
    "Aciona Ventilador Painel",
    "Aciona Ventilador Ponte",
}};

constexpr std::array<const char*, 21> kAnalogInputNames = {{
    "Tensao Fase A",
    "Tensao Fase B",
    "Tensao Fase C",
    "Corrente Fase A",
    "Corrente Fase B",
    "Corrente Fase C",
    "Corrente Neutro",
    "THD Tensao Fase A",
    "THD Tensao Fase B",
    "THD Tensao Fase C",
    "THD Corrente Fase A",
    "THD Corrente Fase B",
    "THD Corrente Fase C",
    "Desequilibrio Negativo",
    "Desequilibrio Zero",
    "Tensao Link CC",
    "Temperatura Ponte",
    "Estado Atual de Operacao",
    "Modo Reativo",
    "Modo Harmonicos",
    "Codigo de Falta",
}};

constexpr std::array<const char*, 5> kAnalogOutputNames = {{
    "Ref. Regulacao de Tensao",
    "Limite Corrente Deseq. Neg.",
    "Limite Corrente Deseq. Zero",
    "Limite Corrente Reativo",
    "Limite Corrente Harmonico",
}};

template <std::size_t N>
constexpr const char* pointName(const std::array<const char*, N>& table, uint16_t index)
{
    if (index < N) {
        return table[index];
    }
    return "unknown";
}

} // namespace dnp3sim
