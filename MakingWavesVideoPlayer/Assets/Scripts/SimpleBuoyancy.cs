using UnityEngine;
using UnityEngine.Rendering.HighDefinition;

[RequireComponent(typeof(Rigidbody))]
public class BuoyancyOnHDRPWater : MonoBehaviour
{
    public float floatHeight = 0.5f;     // how much above the water surface
    public float bounceDamping = 0.1f;   // smoothing vertical velocity
    public float waterDrag = 0.2f;       // drag when in water

    private Rigidbody rb;
    private WaterSurface waterSurface;
    private WaterSearchParameters searchParams;
    private WaterSearchResult searchResult;

    void Start()
    {
        rb = GetComponent<Rigidbody>();

        GameObject oceanObj = GameObject.Find("Ocean");
        if (oceanObj == null)
        {
            Debug.LogError("Buoyancy: No GameObject named 'Ocean' found in scene!");
            return;
        }

        waterSurface = oceanObj.GetComponent<WaterSurface>();
        if (waterSurface == null)
            Debug.LogError("Buoyancy: 'Ocean' has no WaterSurface component.");

        // Initialize search parameters
        searchParams = new WaterSearchParameters();
        searchResult = new WaterSearchResult();
    }

    void FixedUpdate()
    {
        if (waterSurface == null)
            return;

        // Set up the search parameters
        searchParams.startPositionWS = transform.position;
        searchParams.targetPositionWS = transform.position + Vector3.up * 2f;
        searchParams.error = 0.01f;
        // Optionally set maxIterations if needed (older versions require)
        searchParams.maxIterations = 8;

        // Query the water surface
        bool found = waterSurface.ProjectPointOnWaterSurface(searchParams, out searchResult);

        if (!found)
            return;

        float waterY = searchResult.projectedPositionWS.y;
        float depth = waterY - transform.position.y + floatHeight;

        if (depth > 0f)
        {
            // Buoyancy force
            float buoyancyForce = Physics.gravity.magnitude * rb.mass * depth;
            rb.AddForce(Vector3.up * buoyancyForce, ForceMode.Acceleration);

            // Water drag
            rb.AddForce(-rb.linearVelocity * waterDrag, ForceMode.Acceleration);
        }

        // Vertical damping to reduce jitter
        rb.AddForce(Vector3.up * (-rb.linearVelocity.y * bounceDamping), ForceMode.Acceleration);
    }
}