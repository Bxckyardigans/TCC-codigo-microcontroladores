import express, { response } from "express";
import mysql from "mysql2/promise";
import fetch from "node-fetch";

const app = express.Router()


// CONFIGURAÇÃO DO BD
const db = await mysql.createPool({
  host: "localhost",
  user: "root",
  password: "",
  database: "vacinas_db"
});


// FUNÇÃO PARA BUSCAR OS DADOS DO ESP32 RECEPTOR VIA DNS mDNS

async function receberDoESP() {
  try {
    // define o DNS do ESP32 receptor
    const url = "http://coldremote.local/dados";

    const resposta = await fetch(url, { timeout: 3000 });
    const json = await resposta.json();

    console.log("📡 Dados recebidos do ESP32:", json);

    // Salva no MySQL
    await salvarNoBanco(json);

  } catch (erro) {
    console.error("❌ Erro ao receber dados do ESP32:", erro.message);
  }
}


// SALVAR NO BANCO DE DADOS

async function salvarNoBanco(dados) {
  const { temperatura, latitude, longitude} = dados;

  await db.query(
    "INSERT INTO registros_vacinas (temperatura, latitude, longitude) VALUES (?, ?, ?)",
    [temperatura, latitude, longitude]
  );

  console.log("💾 Dados armazenados no MySQL com sucesso!");
}

//busca os dados anteriores no banco
async function buscarNoBanco() {
  try{
  const [rows] = await  db.query("SELECT temperatura, latitude, longitude, recebido_em FROM registros_vacinas")
  console.log(rows)
  return [rows]}catch(error){console.log(error)}
}


// PONTO FINAL PARA O ESP32 RECEPTOR ENVIAR DIRETAMENTE

app.post("/api/registrar", async (req, res) => {
  try {
    await salvarNoBanco(req.body);

    res.json({ status: "ok", mensagem: "Dados gravados com sucesso" });

  } catch (erro) {
    console.error(erro);
    res.status(500).json({ erro: "Falha ao salvar no banco" });
  }
});


app.get("/", async (req,res) =>{
  const dadosAnteriores = await buscarNoBanco()
  if(dadosAnteriores <1){
    return res.status(204).end();
  }
  console.log(dadosAnteriores)
  return res.status(200).send(dadosAnteriores)
})

// vai buscar a cada 5 segundos
setInterval(receberDoESP, 5000);
setInterval(buscarNoBanco, 5000);

export default app;