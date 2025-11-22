import express from "express";
import app from "./server.js";


const rotas = express();

rotas.use("/server",app)


export default rotas;