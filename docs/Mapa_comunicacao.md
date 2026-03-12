# Mapa de Comunicação

Mapeamento dos pontos de comunicação entre Modbus (Python ↔ DSP) e DNP3 (C++ ↔ SCADA).

> Os sinais são referenciados do ponto de vista do SCADA: "Input" = leitura pelo SCADA, "Output" = comando enviado pelo SCADA.

## Modbus

### Coils (leitura/escrita)

| Endereço | Nome                           | Tipo    |
| -------- | ------------------------------ | ------- |
| 0        | Conectar Equipamento           | Binário |
| 1        | Desconectar Equipamento        | Binário |
| 2        | Reset Proteção                 | Binário |
| 3        | Emergência                     | Binário |
| 4        | Ativa Desequilíbrio            | Binário |
| 5        | Desativa Desequilíbrio         | Binário |
| 6        | Ativa Suporte de Tensão        | Binário |
| 7        | Desativa Suporte de Tensão     | Binário |
| 8        | Ativa Regulação de Tensão      | Binário |
| 9        | Desativa Regulação de Tensão   | Binário |
| 10       | Ativa Compensação Harmônica    | Binário |
| 11       | Desativa Compensação Harmônica | Binário |
| 12       | Compensa Harmônica 3           | Binário |
| 13       | Compensa Harmônica 5           | Binário |
| 14       | Compensa Harmônica 7           | Binário |
| 15       | Compensa Harmônica 9           | Binário |
| 16       | Compensa Harmônica 11          | Binário |
| 17       | Compensação Harmônica Completa | Binário |
| 18       | Aciona Ventilador Painel       | Binário |
| 19       | Aciona Ventilador Ponte        | Binário |

### Inputs (somente leitura)

| Endereço | Nome                      | Tipo    |
| -------- | ------------------------- | ------- |
| 0        | Equipamento Energizado    | Binário |
| 1        | Equipamento Operando      | Binário |
| 2        | Alarme                    | Binário |
| 3        | Operação Local/Remoto     | Binário |
| 4        | Botoeira de Emergência    | Binário |
| 5        | Status Desequilíbrio      | Binário |
| 6        | Status Reativo            | Binário |
| 7        | Status Suporte de Tensão  | Binário |
| 8        | Status Regulação de Tensão    | Binário |
| 9        | Status Compensação Harmônica  | Binário |
| 10       | Erro de IGBT              | Binário |

### Holding Registers (leitura/escrita)

| Endereço | Nome                               | Tipo          |
| -------- | ---------------------------------- | ------------- |
| 0        | Referência de Regulação de Tensão  | Float 32 bits |
| 1        | Limite Corrente Desequilíbrio Neg. | Float 32 bits |
| 2        | Limite Corrente Desequilíbrio Zero | Float 32 bits |
| 3        | Limite Corrente Reativo            | Float 32 bits |
| 4        | Limite Corrente Harmônico          | Float 32 bits |

### Input Registers (somente leitura)

| Endereço | Nome                       | Tipo               |
| -------- | -------------------------- | ------------------ |
| 0        | Tensão Fase A              | Float 32 bits      |
| 1        | Tensão Fase B              | Float 32 bits      |
| 2        | Tensão Fase C              | Float 32 bits      |
| 3        | Corrente Fase A            | Float 32 bits      |
| 4        | Corrente Fase B            | Float 32 bits      |
| 5        | Corrente Fase C            | Float 32 bits      |
| 6        | Corrente Neutro            | Float 32 bits      |
| 7        | THD Tensão Fase A          | Float 32 bits      |
| 8        | THD Tensão Fase B          | Float 32 bits      |
| 9        | THD Tensão Fase C          | Float 32 bits      |
| 10       | THD Corrente Fase A        | Float 32 bits      |
| 11       | THD Corrente Fase B        | Float 32 bits      |
| 12       | THD Corrente Fase C        | Float 32 bits      |
| 13       | Desequilíbrio Negativo     | Float 32 bits      |
| 14       | Desequilíbrio Zero         | Float 32 bits      |
| 15       | Tensão Link CC             | Float 32 bits      |
| 16       | Temperatura Ponte          | Float 32 bits      |
| 17       | Estado Atual de Operação   | Unsigned Int 32 bits |
| 18       | Modo Reativo               | Unsigned Int 32 bits |
| 19       | Modo Harmônicos            | Unsigned Int 32 bits |
| 20       | Código de Falta            | Unsigned Int 32 bits |

---

## Mapeamento Modbus ↔ DNP3

| Modbus            | DNP3          |
| ----------------- | ------------- |
| Coils             | Binary Output |
| Inputs            | Binary Input  |
| Holding Registers | Analog Output |
| Input Registers   | Analog Input  |

---

## DNP3

### Binary Input (Modbus Inputs → DNP3)

Status de leitura do equipamento. 11 pontos.

| Id | Informação                    | Classe   | Tipo     |
| -- | ----------------------------- | -------- | -------- |
| 0  | Equipamento Energizado        | Classe 1 | Binária  |
| 1  | Equipamento Operando          | Classe 1 | Binária  |
| 2  | Alarme                        | Classe 1 | Binária  |
| 3  | Operação Local/Remoto         | Classe 1 | Binária  |
| 4  | Estado Botoeira de Emergência | Classe 1 | Binária  |
| 5  | Status Desequilíbrio          | Classe 1 | Binária  |
| 6  | Status Reativo                | Classe 1 | Binária  |
| 7  | Status Suporte de Tensão      | Classe 1 | Binária  |
| 8  | Status Regulação de Tensão    | Classe 1 | Binária  |
| 9  | Status Compensação Harmônica  | Classe 1 | Binária  |
| 10 | Erro de IGBT                  | Classe 1 | Binária  |

---

### Binary Output (Modbus Coils → DNP3)

Comandos enviados pelo SCADA. 20 pontos.

| Id | Informação                     | Classe   | Tipo    |
| -- | ------------------------------ | -------- | ------- |
| 0  | Conectar Equipamento           | Classe 1 | Binária |
| 1  | Desconectar Equipamento        | Classe 1 | Binária |
| 2  | Reset Proteção                 | Classe 1 | Binária |
| 3  | Emergência                     | Classe 1 | Binária |
| 4  | Ativa Desequilíbrio            | Classe 1 | Binária |
| 5  | Desativa Desequilíbrio         | Classe 1 | Binária |
| 6  | Ativa Suporte de Tensão        | Classe 1 | Binária |
| 7  | Desativa Suporte de Tensão     | Classe 1 | Binária |
| 8  | Ativa Regulação de Tensão      | Classe 1 | Binária |
| 9  | Desativa Regulação de Tensão   | Classe 1 | Binária |
| 10 | Ativa Compensação Harmônica    | Classe 1 | Binária |
| 11 | Desativa Compensação Harmônica | Classe 1 | Binária |
| 12 | Compensa Harmônica 3           | Classe 0 | Binária |
| 13 | Compensa Harmônica 5           | Classe 0 | Binária |
| 14 | Compensa Harmônica 7           | Classe 0 | Binária |
| 15 | Compensa Harmônica 9           | Classe 0 | Binária |
| 16 | Compensa Harmônica 11          | Classe 0 | Binária |
| 17 | Compensação Harmônica Completa | Classe 0 | Binária |
| 18 | Aciona Ventilador Painel       | Classe 0 | Binária |
| 19 | Aciona Ventilador Ponte        | Classe 0 | Binária |

---

### Analog Input (Modbus Input Registers → DNP3)

Medições e estados do equipamento. 21 pontos.

| Id | Informação                     | Classe   | Tipo                     |
| -- | ------------------------------ | -------- | ------------------------ |
| 0  | Tensão Fase A                  | Classe 0 | 32-bit (float)           |
| 1  | Tensão Fase B                  | Classe 0 | 32-bit (float)           |
| 2  | Tensão Fase C                  | Classe 0 | 32-bit (float)           |
| 3  | Corrente Fase A                | Classe 0 | 32-bit (float)           |
| 4  | Corrente Fase B                | Classe 0 | 32-bit (float)           |
| 5  | Corrente Fase C                | Classe 0 | 32-bit (float)           |
| 6  | Corrente Neutro                | Classe 0 | 32-bit (float)           |
| 7  | THD Tensão Fase A              | Classe 0 | 32-bit (float)           |
| 8  | THD Tensão Fase B              | Classe 0 | 32-bit (float)           |
| 9  | THD Tensão Fase C              | Classe 0 | 32-bit (float)           |
| 10 | THD Corrente Fase A            | Classe 0 | 32-bit (float)           |
| 11 | THD Corrente Fase B            | Classe 0 | 32-bit (float)           |
| 12 | THD Corrente Fase C            | Classe 0 | 32-bit (float)           |
| 13 | Desequilíbrio Negativo         | Classe 0 | 32-bit (float)           |
| 14 | Desequilíbrio Zero             | Classe 0 | 32-bit (float)           |
| 15 | Tensão Link CC                 | Classe 0 | 32-bit (float)           |
| 16 | Temperatura Ponte              | Classe 0 | 32-bit (float)           |
| 17 | Estado Atual de Operação       | Classe 1 | 16-bit (inteiro sem sinal) |
| 18 | Modo Reativo                   | Classe 1 | 16-bit (inteiro sem sinal) |
| 19 | Modo Harmônicos                | Classe 1 | 16-bit (inteiro sem sinal) |
| 20 | Código de Falta                | Classe 1 | 16-bit (inteiro sem sinal) |

---

### Analog Output (Modbus Holding Registers → DNP3)

Setpoints enviados pelo SCADA. 5 pontos.

| Id | Informação                         | Classe   | Tipo           |
| -- | ---------------------------------- | -------- | -------------- |
| 0  | Referência de Regulação de Tensão  | Classe 2 | 32-bit (float) |
| 1  | Limite Corrente Desequilíbrio Neg. | Classe 2 | 32-bit (float) |
| 2  | Limite Corrente Desequilíbrio Zero | Classe 2 | 32-bit (float) |
| 3  | Limite Corrente Reativo            | Classe 2 | 32-bit (float) |
| 4  | Limite Corrente Harmônico          | Classe 2 | 32-bit (float) |
