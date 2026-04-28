function generateCode() {
    const input = document.getElementById("input").value;

    if (!input) {
        alert("Please enter a link");
        return;
    }

    // jednoduchý fake kód (zatím bez backendu)
    const code = Math.floor(100000 + Math.random() * 900000);

    document.getElementById("result").innerHTML =
        "Your code: <br><span style='font-size:32px'>" + code + "</span>";
}
