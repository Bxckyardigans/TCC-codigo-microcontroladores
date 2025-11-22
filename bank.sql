CREATE TABLE registros_vacinas (
    id INT AUTO_INCREMENT PRIMARY KEY,
    temperatura FLOAT NOT NULL,
    latitude DOUBLE NOT NULL,
    longitude DOUBLE NOT NULL,
    dataHora DATETIME NOT NULL,
    recebido_em TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
