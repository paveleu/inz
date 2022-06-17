const express = require('express')
const app = express()
const port = 3000

app.get('*', (req, res) => {


  console.log("New Req!");
  console.log(req.path);
  console.log(req.query);

  console.log("********");

  res.send('0')
})

app.listen(port, () => {
  console.log(`Example app listening on port ${port}`)
})