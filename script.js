const API_URL = "https://orbis-api.jsifta52.workers.dev";

async function generateCode() {
    const input = document.getElementById("input").value;

    if (!input) {
        alert("Zadej URL");
        return;
    }

    try {
        const res = await fetch(API_URL + "/create", {
            method: "POST",
            headers: {
                "Content-Type": "application/json"
            },
            body: JSON.stringify({
                url: input
            })
        });

        const data = await res.json();

        if (data.code) {
            document.getElementById("result").innerHTML =
                "✔ CODE: <br><b style='font-size:32px'>" + data.code + "</b>";
        } else {
            document.getElementById("result").innerText =
                "Error: " + JSON.stringify(data);
        }

    } catch (err) {
        document.getElementById("result").innerText =
            "Network error";
    }
}
