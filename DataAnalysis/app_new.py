from flask import Flask, render_template, request, flash
from joblib import load
from datetime import datetime, timedelta

app = Flask(__name__)
app.secret_key = 'some_secret_key'  # This is required for using flash messages

# Load the models
avg_model_results = load("sarima_weekly_mean_model.pkl")
max_model_results = load("sarima_weekly_max_model.pkl")
min_model_results = load("sarima_weekly_min_model.pkl")

def get_ordinal(n):
    if 10 <= n % 100 <= 20:
        suffix = 'th'
    else:
        suffix = {1: 'st', 2: 'nd', 3: 'rd'}.get(n % 10, 'th')
    return suffix


@app.route("/", methods=["GET", "POST"])
def index():
    prediction_avg = None
    prediction_max = None
    prediction_min = None
    formatted_date = None
    target_date = None

    # Load the historical data for each model
    historical_data_avg_all = avg_model_results.data.orig_endog.tolist()
    historical_data_max_all = max_model_results.data.orig_endog.tolist()
    historical_data_min_all = min_model_results.data.orig_endog.tolist()

    historical_dates_all = [str(date.date()) for date in avg_model_results.data.orig_endog.index]

    if request.method == "POST":
        target_date = datetime.strptime(request.form["date"], "%Y-%m-%d")
    else:
        # If not a POST request, default to forecasting for 52 weeks ahead (1 year)
        target_date = datetime.strptime(historical_dates_all[-1], "%Y-%m-%d") + timedelta(weeks=52)

    weeks_difference = (target_date - datetime.strptime(historical_dates_all[-1], "%Y-%m-%d")).days // 7

    # Get the index from which we should start displaying the historical data (one year before prediction)
    start_index = max(0, len(historical_data_avg_all) - 52)

    historical_data_avg = historical_data_avg_all[start_index:]
    historical_data_max = historical_data_max_all[start_index:]
    historical_data_min = historical_data_min_all[start_index:]
    historical_dates = historical_dates_all[start_index:]

    # Generate forecasts for each model
    forecast_avg = avg_model_results.get_forecast(steps=weeks_difference+1)
    forecast_max = max_model_results.get_forecast(steps=weeks_difference+1)
    forecast_min = min_model_results.get_forecast(steps=weeks_difference+1)

    prediction_avg = forecast_avg.predicted_mean.iloc[-1]
    prediction_max = forecast_max.predicted_mean.iloc[-1]
    prediction_min = forecast_min.predicted_mean.iloc[-1]

    # Append the forecasted data to the historical data for plotting
    historical_data_avg.extend(forecast_avg.predicted_mean.tolist())
    historical_data_max.extend(forecast_max.predicted_mean.tolist())
    historical_data_min.extend(forecast_min.predicted_mean.tolist())

    # Extend the dates as well
    forecast_dates = [str(date.date()) for date in forecast_avg.predicted_mean.index]
    historical_dates.extend(forecast_dates)

    formatted_date = target_date.strftime('%B %d') + get_ordinal(target_date.day) + " " + target_date.strftime('%Y')

    return render_template("index_new.html", prediction_avg=prediction_avg, prediction_max=prediction_max, prediction_min=prediction_min, formatted_date=formatted_date, historical_data_avg=historical_data_avg, historical_data_max=historical_data_max, historical_data_min=historical_data_min, historical_dates=historical_dates)



if __name__ == "__main__":
    app.run(debug=True)
