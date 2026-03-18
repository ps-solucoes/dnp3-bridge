# Mapa de Comunicação SCADA v1

Mapeamento original dos pontos DNP3 entre o equipamento e o SCADA.

> Os sinais são referenciados do ponto de vista do SCADA: "Input" = leitura pelo SCADA, "Output" = comando enviado pelo SCADA.

---

## Binary Input

Status de leitura do equipamento. 4 pontos.

| Id | Informação             | Classe   | Tipo    | Valor 0                          | Valor 1                |
| -- | ---------------------- | -------- | ------- | -------------------------------- | ---------------------- |
| 0  | Equipamento Energizado | Classe 1 | Binária | Sem presença de tensão           | Presença de tensão     |
| 1  | Equipamento Operando   | Classe 1 | Binária | Equipamento não está em operação | Equipamento em operação |
| 2  | Alarme                 | Classe 1 | Binária | Sem alarme                       | Com alarme             |
| 3  | Operação Local/Remoto  | Classe 1 | Binária | Local                            | Remoto                 |

---

## Binary Output

Comandos enviados pelo SCADA. 3 pontos.

| Id | Informação          | Classe   | Tipo    | Valor 0                    | Valor 1                | Detalhes                                                                                                                                                                      |
| -- | ------------------- | -------- | ------- | -------------------------- | ---------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0  | Ligar Equipamento   | Classe 1 | Binária | Não ligar equipamento      | Ligar equipamento      | Envia comando para iniciar a operação do equipamento                                                                                                                          |
| 1  | Desligar Equipamento | Classe 1 | Binária | Não desligar o equipamento | Desligar o equipamento | Envia comando para desligar o equipamento                                                                                                                                     |
| 2  | Reset Proteção      | Classe 1 | Binária | Não reseta a falta         | Resetar a falta        | Caso alguma falta tenha ocorrido este ponto permite resetar a falta para partir novamente o equipamento. Somente algumas faltas permitem ser resetadas, outras necessitam que uma equipe seja enviada para verificar o equipamento. |

---

## Analog Input

Medições do equipamento. 8 pontos.

| Id | Informação      | Classe   | Tipo              | Detalhes                                              |
| -- | --------------- | -------- | ----------------- | ----------------------------------------------------- |
| 0  | Tensão Fase A   | Classe 2 | 16-bit (inteiro)  | Valor RMS de tensão da fase A                         |
| 1  | Tensão Fase B   | Classe 2 | 16-bit (inteiro)  | Valor RMS de tensão da fase B                         |
| 2  | Tensão Fase C   | Classe 2 | 16-bit (inteiro)  | Valor RMS de tensão da fase C                         |
| 3  | Corrente Fase A | Classe 2 | 16-bit (inteiro)  | Valor RMS de corrente da fase A do equipamento        |
| 4  | Corrente Fase B | Classe 2 | 16-bit (inteiro)  | Valor RMS de corrente da fase B do equipamento        |
| 5  | Corrente Fase C | Classe 2 | 16-bit (inteiro)  | Valor RMS de corrente da fase C do equipamento        |
| 6  | Corrente Neutro | Classe 2 | 16-bit (inteiro)  | Valor RMS de corrente da corrente Neutro do equipamento |
| 20 | Código de Falta | Classe 2 | 16-bit (inteiro)  | Caso alguma falta ocorra informa o código da falta    |
