"""
Shared point name tables and realistic data generation for the DSP simulator.

These constants mirror the communication map between the Python application
and the dnp3-bridge C++ service.
"""

import random

# ---------------------------------------------------------------------------
# Point name tables
# ---------------------------------------------------------------------------
BINARY_INPUT_NAMES: dict[int, str] = {
    0: "Equipamento Energizado",
    1: "Equipamento Operando",
    2: "Alarme",
    3: "Operacao Local/Remoto",
    4: "Botoeira de Emergencia",
    5: "Status Desequilibrio",
    6: "Status Reativo",
    7: "Status Suporte de Tensao",
    8: "Status Regulacao de Tensao",
    9: "Status Compensacao Harmonica",
    10: "Erro de IGBT",
}

ANALOG_INPUT_NAMES: dict[int, str] = {
    0: "Tensao Fase A",
    1: "Tensao Fase B",
    2: "Tensao Fase C",
    3: "Corrente Fase A",
    4: "Corrente Fase B",
    5: "Corrente Fase C",
    6: "Corrente Neutro",
    7: "THD Tensao Fase A",
    8: "THD Tensao Fase B",
    9: "THD Tensao Fase C",
    10: "THD Corrente Fase A",
    11: "THD Corrente Fase B",
    12: "THD Corrente Fase C",
    13: "Desequilibrio Negativo",
    14: "Desequilibrio Zero",
    15: "Tensao Link CC",
    16: "Temperatura Ponte",
    17: "Estado Atual de Operacao",
    18: "Modo Reativo",
    19: "Modo Harmonicos",
    20: "Codigo de Falta",
}

BINARY_OUTPUT_NAMES: dict[int, str] = {
    0: "Conectar Equipamento",
    1: "Desconectar Equipamento",
    2: "Reset Protecao",
    3: "Emergencia",
    4: "Ativa Desequilibrio",
    5: "Desativa Desequilibrio",
    6: "Ativa Suporte de Tensao",
    7: "Desativa Suporte de Tensao",
    8: "Ativa Regulacao de Tensao",
    9: "Desativa Regulacao de Tensao",
    10: "Ativa Compensacao Harmonica",
    11: "Desativa Compensacao Harmonica",
    12: "Compensa Harmonica 3",
    13: "Compensa Harmonica 5",
    14: "Compensa Harmonica 7",
    15: "Compensa Harmonica 9",
    16: "Compensa Harmonica 11",
    17: "Compensacao Harmonica Completa",
    18: "Aciona Ventilador Painel",
    19: "Aciona Ventilador Ponte",
}

ANALOG_OUTPUT_NAMES: dict[int, str] = {
    0: "Ref. Regulacao de Tensao",
    1: "Limite Corrente Deseq. Neg.",
    2: "Limite Corrente Deseq. Zero",
    3: "Limite Corrente Reativo",
    4: "Limite Corrente Harmonico",
}

# ---------------------------------------------------------------------------
# Realistic default values
# ---------------------------------------------------------------------------
REALISTIC_BINARY_DEFAULTS: dict[int, bool] = {
    0: True,   # Energizado
    1: True,   # Operando
    2: False,  # Alarme
    3: True,   # Local/Remoto
    4: False,  # Botoeira Emergencia
    5: False,  # Status Desequilibrio
    6: True,   # Status Reativo
    7: False,  # Status Suporte Tensao
    8: False,  # Status Regulacao Tensao
    9: True,   # Status Comp. Harmonica
    10: False, # Erro IGBT
}


def generate_realistic_analogs() -> list[tuple[int, float]]:
    """Generate realistic analog point values."""
    return [
        (0, random.gauss(220.0, 2.0)),
        (1, random.gauss(220.0, 2.0)),
        (2, random.gauss(220.0, 2.0)),
        (3, random.gauss(15.0, 1.0)),
        (4, random.gauss(15.0, 1.0)),
        (5, random.gauss(15.0, 1.0)),
        (6, random.gauss(0.5, 0.1)),
        (7, random.uniform(2.0, 5.0)),
        (8, random.uniform(2.0, 5.0)),
        (9, random.uniform(2.0, 5.0)),
        (10, random.uniform(2.0, 5.0)),
        (11, random.uniform(2.0, 5.0)),
        (12, random.uniform(2.0, 5.0)),
        (13, random.uniform(0.5, 2.0)),
        (14, random.uniform(0.5, 2.0)),
        (15, random.gauss(650.0, 5.0)),
        (16, random.gauss(45.0, 3.0)),
        (17, 1.0),
        (18, 2.0),
        (19, 0.0),
        (20, 0.0),
    ]
